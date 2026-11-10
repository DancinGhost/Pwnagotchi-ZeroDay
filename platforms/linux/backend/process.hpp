#pragma once

// Minimal POSIX subprocess helpers for the CLI-tool WiFi backend.
//
// Uses fork/execvp with explicit argv vectors (never a shell string), so tool arguments
// such as interface names and BSSIDs cannot be interpreted as shell metacharacters.
// Two shapes are provided:
//   - runCapture(): run a short command to completion, capturing stdout.
//   - DetachedProcess: launch a long-running tool (airodump-ng) and later signal/kill it.

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace zd {
namespace linux_backend {

struct CommandResult {
    int exitCode = -1;
    bool launched = false;
    std::string stdoutText;
};

// Run argv[0] with argv to completion, capturing stdout. Blocks until exit. Intended for
// fast commands (iw scan, nmcli, airmon-ng start). Returns exitCode == 0 on success.
inline CommandResult runCapture(const std::vector<std::string>& argv) {
    CommandResult res;
    if (argv.empty()) {
        return res;
    }
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return res;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return res;
    }

    if (pid == 0) {
        // Child: redirect stdout (and stderr) to the pipe, then exec.
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const std::string& a : argv) {
            cargv.push_back(const_cast<char*>(a.c_str()));
        }
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);  // exec failed
    }

    // Parent: read all stdout.
    close(pipefd[1]);
    res.launched = true;
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        res.stdoutText.append(buf, static_cast<std::size_t>(n));
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) == pid && WIFEXITED(status)) {
        res.exitCode = WEXITSTATUS(status);
    }
    return res;
}

// A long-running child process we control (e.g. airodump-ng writing CSV to disk). Its
// stdout/stderr are sent to /dev/null; we interact with it only through its output files
// and by signalling it. Call stop() to SIGTERM (then SIGKILL) it.
class DetachedProcess {
public:
    DetachedProcess() = default;
    DetachedProcess(const DetachedProcess&) = delete;
    DetachedProcess& operator=(const DetachedProcess&) = delete;
    ~DetachedProcess() { stop(); }

    bool start(const std::vector<std::string>& argv) {
        if (running_ || argv.empty()) {
            return false;
        }
        const pid_t pid = fork();
        if (pid < 0) {
            return false;
        }
        if (pid == 0) {
            const int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO) {
                    close(devnull);
                }
            }
            std::vector<char*> cargv;
            cargv.reserve(argv.size() + 1);
            for (const std::string& a : argv) {
                cargv.push_back(const_cast<char*>(a.c_str()));
            }
            cargv.push_back(nullptr);
            execvp(cargv[0], cargv.data());
            _exit(127);
        }
        pid_ = pid;
        running_ = true;
        return true;
    }

    bool running() const {
        if (!running_) {
            return false;
        }
        int status = 0;
        const pid_t r = waitpid(pid_, &status, WNOHANG);
        if (r == pid_) {
            running_ = false;  // reaped
        }
        return running_;
    }

    void stop() {
        if (!running_) {
            return;
        }
        ::kill(pid_, SIGTERM);
        for (int i = 0; i < 20; ++i) {
            int status = 0;
            if (waitpid(pid_, &status, WNOHANG) == pid_) {
                running_ = false;
                return;
            }
            usleep(50 * 1000);  // 50ms
        }
        ::kill(pid_, SIGKILL);
        int status = 0;
        waitpid(pid_, &status, 0);
        running_ = false;
    }

    pid_t pid() const { return pid_; }

private:
    mutable bool running_ = false;
    pid_t pid_ = -1;
};

}  // namespace linux_backend
}  // namespace zd
