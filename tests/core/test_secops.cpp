// Host unit tests for the portable security engine (core/include/zd/secops.hpp).
// Pure, hardware-free: exercises the reducer, defensive monitors, and the authorization gate.

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "zd/secops.hpp"
#include "zd/secops_hal.hpp"

namespace {

using namespace zd::sec;

bool hasAlert(const SecState& s, AlertKind k, const std::string& bssidSubstr = "") {
    for (const Alert& a : s.alerts) {
        if (a.kind == k && (bssidSubstr.empty() || a.bssid.find(bssidSubstr) != std::string::npos)) {
            return true;
        }
    }
    return false;
}

std::size_t countAlerts(const SecState& s, AlertKind k) {
    std::size_t n = 0;
    for (const Alert& a : s.alerts) {
        if (a.kind == k) {
            ++n;
        }
    }
    return n;
}

void testMacNormalization() {
    assert(normalizeMac("AA:BB:CC:DD:EE:FF") == "aa:bb:cc:dd:ee:ff");
    assert(normalizeMac("aabbccddeeff") == "aa:bb:cc:dd:ee:ff");
    assert(normalizeMac("AA-BB-CC-DD-EE-FF") == "aa:bb:cc:dd:ee:ff");
    assert(normalizeMac("  aa:bb:cc:dd:ee:ff ") == "aa:bb:cc:dd:ee:ff");
    assert(isBroadcastMac(normalizeMac("ff:ff:ff:ff:ff:ff")));
}

void testMacValidation() {
    assert(isValidMac("AA:BB:CC:DD:EE:FF"));
    assert(isValidMac("aabbccddeeff"));
    assert(isValidMac("AA-BB-CC-DD-EE-FF"));
    assert(isValidMac("  aa:bb:cc:dd:ee:ff "));
    assert(!isValidMac(""));
    assert(!isValidMac("not-a-mac"));
    assert(!isValidMac("--i-am-authorized"));       // a stray CLI flag, not a MAC
    assert(!isValidMac("aa:bb:cc:dd:ee"));           // too short (10 hex digits)
    assert(!isValidMac("aa:bb:cc:dd:ee:ff:00"));     // too long (14 hex digits)
}

void testChannelBands() {
    assert(classifyChannel(1) == Band::Band24GHz);
    assert(classifyChannel(14) == Band::Band24GHz);
    assert(classifyChannel(36) == Band::Band5GHz);
    assert(classifyChannel(149) == Band::Band5GHz);
    assert(classifyChannel(177) == Band::Band5GHz);
    assert(classifyChannel(0) == Band::Unknown);
    assert(classifyChannel(20) == Band::Unknown);
    assert(classifyChannel(200) == Band::Unknown);

    SecState s;
    s = reduce(s, ApObserved{0u, "aa:bb:cc:00:00:01", "A", 6, -40, Encryption::Wpa2});
    s = reduce(s, ApObserved{0u, "aa:bb:cc:00:00:02", "B", 6, -40, Encryption::Wpa2});
    s = reduce(s, ApObserved{0u, "aa:bb:cc:00:00:03", "C", 149, -40, Encryption::Wpa2});

    const std::vector<ChannelCount> dist = channelDistribution(s);
    assert(dist.size() == 2u);
    assert(dist[0].channel == 6 && dist[0].band == Band::Band24GHz && dist[0].count == 2u);
    assert(dist[1].channel == 149 && dist[1].band == Band::Band5GHz && dist[1].count == 1u);
}

void testAuthorizationGate() {
    Authorization auth;
    // Default: everything blocked (unconfirmed).
    assert(authorize(auth, "aa:bb:cc:dd:ee:ff") == AuthDecision::BlockedUnconfirmed);

    // Confirmed but no allow-list entry => still blocked.
    auth.operatorConfirmed = true;
    assert(authorize(auth, "aa:bb:cc:dd:ee:ff") == AuthDecision::BlockedNotAllowlisted);

    // Allow-listed (case/format-insensitive) => allowed.
    auth.allow("AABBCCDDEEFF");
    assert(authorize(auth, "aa:bb:cc:dd:ee:ff") == AuthDecision::Allowed);
    assert(authorize(auth, "11:22:33:44:55:66") == AuthDecision::BlockedNotAllowlisted);

    // allowAll wildcard still requires confirmation.
    Authorization wild;
    wild.allowAll = true;
    assert(authorize(wild, "aa:bb:cc:dd:ee:ff") == AuthDecision::BlockedUnconfirmed);
    wild.operatorConfirmed = true;
    assert(authorize(wild, "any") == AuthDecision::Allowed);
}

void testApTrackingAndEvilTwin() {
    SecState s;
    s = reduce(s, ApObserved{1000u, "aa:bb:cc:00:00:01", "HomeNet", 6, -40, Encryption::Wpa2});
    assert(s.targets.size() == 1u);
    assert(findTarget(s, "aa:bb:cc:00:00:01") != nullptr);
    assert(!hasAlert(s, AlertKind::EvilTwin));

    // Second BSSID advertising the SAME SSID => evil twin.
    s = reduce(s, ApObserved{1100u, "aa:bb:cc:00:00:02", "HomeNet", 11, -55, Encryption::Wpa2});
    assert(s.targets.size() == 2u);
    assert(hasAlert(s, AlertKind::EvilTwin, "00:00:02"));
}

void testOpenDowngrade() {
    SecState s;
    s = reduce(s, ApObserved{1000u, "aa:bb:cc:00:00:01", "SecureCorp", 6, -45, Encryption::Wpa2});
    // A different BSSID now broadcasts the same SSID as Open => downgrade/impersonation.
    s = reduce(s, ApObserved{1050u, "de:ad:be:ef:00:99", "SecureCorp", 6, -50, Encryption::Open});
    assert(hasAlert(s, AlertKind::OpenDowngrade, "de:ad:be:ef:00:99"));
}

void testDeauthFloodDetection() {
    SecState s;
    s = reduce(s, ApObserved{0u, "aa:bb:cc:00:00:01", "Victim", 6, -40, Encryption::Wpa2});

    // Fewer than the threshold within the window => no flood alert yet.
    for (uint32_t i = 0; i < kDeauthFloodThreshold - 1u; ++i) {
        s = reduce(s, DeauthObserved{100u + i * 10u, "aa:bb:cc:00:00:01", "", ""});
    }
    assert(!hasAlert(s, AlertKind::DeauthFlood));

    // One more within the window crosses the threshold.
    s = reduce(s, DeauthObserved{100u + kDeauthFloodThreshold * 10u, "aa:bb:cc:00:00:01", "", ""});
    assert(hasAlert(s, AlertKind::DeauthFlood, "00:00:01"));
    assert(s.totalDeauths == kDeauthFloodThreshold);

    // Cooldown: a second immediate flood does not spam a new alert.
    const std::size_t before = countAlerts(s, AlertKind::DeauthFlood);
    s = reduce(s, DeauthObserved{100u + kDeauthFloodThreshold * 10u + 5u, "aa:bb:cc:00:00:01", "", ""});
    assert(countAlerts(s, AlertKind::DeauthFlood) == before);
}

void testDeauthWindowExpiry() {
    SecState s;
    // Spread deauths far apart so the sliding window never fills => no flood.
    for (uint32_t i = 0; i < kDeauthFloodThreshold + 4u; ++i) {
        s = reduce(s, DeauthObserved{i * (kDeauthWindowMs + 1000u), "aa:bb:cc:00:00:07", "", ""});
    }
    assert(!hasAlert(s, AlertKind::DeauthFlood));
}

void testKarmaBeacon() {
    SecState s;
    const std::string bssid = "ca:fe:00:00:00:01";
    const char* ssids[] = {"Starbucks", "attwifi", "xfinitywifi", "HomeNet",
                           "Guest", "Airport_Free", "FreeWiFi"};
    for (std::size_t i = 0; i < sizeof(ssids) / sizeof(ssids[0]); ++i) {
        s = reduce(s, ApObserved{static_cast<uint32_t>(1000u + i), bssid, ssids[i], 6, -30,
                                 Encryption::Open});
    }
    assert(hasAlert(s, AlertKind::KarmaBeacon, bssid));
}

void testWpsFlag() {
    SecState s;
    s = reduce(s, ApObserved{1000u, "aa:bb:cc:00:00:01", "NoWps", 6, -40, Encryption::Wpa2, false});
    assert(!findTarget(s, "aa:bb:cc:00:00:01")->wps);
    s = reduce(s, ApObserved{1100u, "aa:bb:cc:00:00:02", "HasWps", 6, -40, Encryption::Wpa2, true});
    assert(findTarget(s, "aa:bb:cc:00:00:02")->wps);
    // A later observation without the flag updates it back down (reflects live state).
    s = reduce(s, ApObserved{1200u, "aa:bb:cc:00:00:02", "HasWps", 6, -40, Encryption::Wpa2, false});
    assert(!findTarget(s, "aa:bb:cc:00:00:02")->wps);
}

void testStationProbeTracking() {
    SecState s;
    const std::string client = "11:22:33:44:55:66";

    s = reduce(s, ProbeRequestObserved{1000u, client, "HomeNet"});
    assert(findStation(s, client) != nullptr);
    assert(findStation(s, client)->probedSsids.size() == 1u);
    assert(findStation(s, client)->probeCount == 1u);

    // Repeated SSID doesn't duplicate; a new one is appended.
    s = reduce(s, ProbeRequestObserved{1100u, client, "HomeNet"});
    s = reduce(s, ProbeRequestObserved{1200u, client, "CoffeeShop"});
    const Station* st = findStation(s, client);
    assert(st->probeCount == 3u);
    assert(st->probedSsids.size() == 2u);

    // Broadcast probes with no station MAC don't create a phantom station.
    s = reduce(s, ProbeRequestObserved{1300u, "ff:ff:ff:ff:ff:ff", "Ignored"});
    assert(findStation(s, "ff:ff:ff:ff:ff:ff") == nullptr);

    // Association (ClientObserved) upserts the same station record and sets lastBssid.
    s = reduce(s, ClientObserved{1400u, "aa:bb:cc:00:00:01", client});
    st = findStation(s, client);
    assert(st->lastBssid == "aa:bb:cc:00:00:01");
    assert(st->probedSsids.size() == 2u);  // untouched by association

    // stationsByActivity sorts most-active first.
    s = reduce(s, ProbeRequestObserved{1500u, "77:88:99:aa:bb:cc", "Solo"});
    const std::vector<Station> ranked = stationsByActivity(s);
    assert(ranked.front().mac == client);
}

void testProbedSsidCap() {
    SecState s;
    const std::string client = "aa:aa:aa:aa:aa:aa";
    for (std::size_t i = 0; i < kMaxProbedSsidsPerStation + 5u; ++i) {
        s = reduce(s, ProbeRequestObserved{static_cast<uint32_t>(1000u + i), client,
                                           "Net" + std::to_string(i)});
    }
    assert(findStation(s, client)->probedSsids.size() == kMaxProbedSsidsPerStation);
}

void testSecMood() {
    SecState s;
    assert(deriveSecMood(s, 10000u) == SecMood::Calm);

    s = reduce(s, ApObserved{1000u, "aa:bb:cc:00:00:01", "HomeNet", 6, -40, Encryption::Wpa2});
    s = reduce(s, ApObserved{1100u, "aa:bb:cc:00:00:02", "HomeNet", 11, -55, Encryption::Wpa2});
    assert(hasAlert(s, AlertKind::EvilTwin));  // Warn severity
    assert(deriveSecMood(s, 1200u) == SecMood::Watchful);

    s = reduce(s, ApObserved{2000u, "de:ad:be:ef:00:99", "HomeNet", 6, -50, Encryption::Open});
    // Need a prior encrypted entry with the same SSID to trigger OpenDowngrade (Critical);
    // reuse the earlier encrypted "HomeNet" AP already present above.
    assert(hasAlert(s, AlertKind::OpenDowngrade));
    assert(deriveSecMood(s, 2100u) == SecMood::Alarmed);

    // Outside the recency window, the alarm fades back to calm.
    assert(deriveSecMood(s, 2100u + 60000u, 30000u) == SecMood::Calm);
}

void testSessionRecordReplay() {
    SecState original;
    std::vector<SecEvent> events = {
        ApObserved{1000u, "aa:bb:cc:00:00:01", "Weird|Name\\With\nNewline", 6, -40,
                   Encryption::Wpa2, true},
        ClientObserved{1100u, "aa:bb:cc:00:00:01", "11:22:33:44:55:66"},
        ProbeRequestObserved{1150u, "11:22:33:44:55:66", "CoffeeShop"},
        DataFrameObserved{1200u, "aa:bb:cc:00:00:01"},
        DeauthObserved{1300u, "aa:bb:cc:00:00:01", "src", "dst"},
        HandshakeObserved{1400u, "aa:bb:cc:00:00:01"},
        PmkidObserved{1500u, "aa:bb:cc:00:00:01"},
        InjectionAttempt{1600u, "aa:bb:cc:00:00:01", "", 5u, AuthDecision::BlockedUnconfirmed},
    };
    for (const SecEvent& ev : events) {
        original = reduce(original, ev);
    }

    const std::vector<std::string> lines = encodeSecEvents(events);
    const std::vector<SecEvent> decoded = decodeSecEvents(lines);
    assert(decoded.size() == events.size());

    const SecState replayed = replaySecEvents(decoded);
    assert(replayed.targets.size() == original.targets.size());
    assert(findTarget(replayed, "aa:bb:cc:00:00:01")->ssid ==
           findTarget(original, "aa:bb:cc:00:00:01")->ssid);
    assert(findTarget(replayed, "aa:bb:cc:00:00:01")->wps);
    assert(replayed.totalDeauths == original.totalDeauths);
    assert(replayed.totalHandshakes == original.totalHandshakes);
    assert(replayed.totalPmkids == original.totalPmkids);
    assert(replayed.blockedInjections == original.blockedInjections);
    assert(replayed.stations.size() == original.stations.size());
    assert(findStation(replayed, "11:22:33:44:55:66")->probedSsids ==
           findStation(original, "11:22:33:44:55:66")->probedSsids);

    // A corrupt/truncated line is skipped, not fatal.
    std::vector<std::string> withJunk = lines;
    withJunk.push_back("TOTALLY|BOGUS");
    withJunk.push_back("");
    assert(decodeSecEvents(withJunk).size() == events.size());
}

void testHandshakeAndPmkid() {
    SecState s;
    s = reduce(s, ApObserved{1000u, "aa:bb:cc:00:00:01", "Target", 6, -40, Encryption::Wpa2});
    s = reduce(s, HandshakeObserved{1500u, "aa:bb:cc:00:00:01"});
    assert(findTarget(s, "aa:bb:cc:00:00:01")->handshake);
    assert(s.totalHandshakes == 1u);
    // Idempotent: re-observing the handshake does not double-count.
    s = reduce(s, HandshakeObserved{1600u, "aa:bb:cc:00:00:01"});
    assert(s.totalHandshakes == 1u);

    s = reduce(s, PmkidObserved{1700u, "aa:bb:cc:00:00:01"});
    assert(findTarget(s, "aa:bb:cc:00:00:01")->pmkid);
    assert(s.totalPmkids == 1u);
    assert(hasAlert(s, AlertKind::HandshakeCaptured));
    assert(hasAlert(s, AlertKind::PmkidCaptured));
}

// A fake injector proving the SecSession gate blocks TX unless authorized.
class SpyInjector : public IInjector {
public:
    int sent = 0;
    bool available() const override { return true; }
    InjectResult deauth(const std::string& bssid, const std::string&, uint32_t count) override {
        sent += static_cast<int>(count);
        InjectResult r;
        r.transmitted = true;
        r.detail = "spy sent " + std::to_string(count) + " to " + bssid;
        return r;
    }
};

void testSessionDeauthGate() {
    SecSession session;
    SpyInjector inj;
    const std::string bssid = "aa:bb:cc:00:00:01";

    // Unconfirmed => blocked, nothing transmitted, TxBlocked alert recorded.
    InjectResult r1 = session.requestDeauth(inj, bssid, "", 5u, 1000u);
    assert(r1.decision == AuthDecision::BlockedUnconfirmed);
    assert(!r1.transmitted);
    assert(inj.sent == 0);
    assert(hasAlert(session.state(), AlertKind::TxBlocked));
    assert(session.state().blockedInjections == 1u);

    // Confirm + allow-list => transmit proceeds.
    session.authorization().operatorConfirmed = true;
    session.authorization().allow(bssid);
    InjectResult r2 = session.requestDeauth(inj, bssid, "", 5u, 2000u);
    assert(r2.decision == AuthDecision::Allowed);
    assert(r2.transmitted);
    assert(inj.sent == 5);
    assert(session.state().allowedInjections == 1u);
}

void testBeginDeauthMatchesRequestDeauth() {
    // beginDeauth() is the fast/synchronous half that requestDeauth() is now built from;
    // verify it gates and records identically when driven by hand (as an async front-end
    // would, running the injector itself off-thread).
    SecSession session;
    SpyInjector inj;
    const std::string bssid = "aa:bb:cc:00:00:09";

    AuthDecision d = session.beginDeauth(bssid, "", 3u, 1000u);
    assert(d == AuthDecision::BlockedUnconfirmed);
    assert(session.state().blockedInjections == 1u);
    assert(inj.sent == 0);  // beginDeauth() must never touch the injector

    session.authorization().operatorConfirmed = true;
    session.authorization().allow(bssid);
    d = session.beginDeauth(bssid, "", 3u, 2000u);
    assert(d == AuthDecision::Allowed);
    assert(session.state().allowedInjections == 1u);
    assert(inj.sent == 0);  // still never touched; caller runs the injector itself
    const InjectResult r = inj.deauth(bssid, "", 3u);
    assert(r.transmitted && inj.sent == 3);
}

void testSessionIngest() {
    SecSession session;
    session.ingest(CapturedFrame{10u, FrameKind::Beacon, "aa:bb:cc:00:00:01", "", "",
                                 "Lab", 6, -42, Encryption::Wpa2});
    session.ingest(CapturedFrame{20u, FrameKind::Eapol, "aa:bb:cc:00:00:01", "", "", "", 6, -42,
                                 Encryption::Wpa2});
    assert(session.state().totalHandshakes == 1u);
    const WifiTarget* t = findTarget(session.state(), "aa:bb:cc:00:00:01");
    assert(t != nullptr && t->ssid == "Lab" && t->handshake);
}

}  // namespace

int main() {
    testMacNormalization();
    testMacValidation();
    testChannelBands();
    testAuthorizationGate();
    testApTrackingAndEvilTwin();
    testOpenDowngrade();
    testDeauthFloodDetection();
    testDeauthWindowExpiry();
    testKarmaBeacon();
    testWpsFlag();
    testStationProbeTracking();
    testProbedSsidCap();
    testSecMood();
    testSessionRecordReplay();
    testHandshakeAndPmkid();
    testSessionDeauthGate();
    testBeginDeauthMatchesRequestDeauth();
    testSessionIngest();
    std::cout << "[secops] all security-engine tests passed\n";
    return 0;
}
