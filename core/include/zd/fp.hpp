#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <type_traits>
#include <utility>
#include <vector>

namespace fp {

// Helper for exhaustive pattern-matching over std::variant sum types.
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// Pure constexpr value clamping (avoids Arduino min/max macro collisions).
template <typename T>
constexpr T clampVal(const T& value, const T& low, const T& high) {
    return (value < low) ? low : ((high < value) ? high : value);
}

// Left-to-right function pipeline: pipe(x, f, g, h) == h(g(f(x)))
template <typename T>
constexpr T pipe(T&& value) {
    return std::forward<T>(value);
}

template <typename T, typename Fn, typename... Rest>
constexpr auto pipe(T&& value, Fn&& fn, Rest&&... rest) {
    return pipe(std::forward<Fn>(fn)(std::forward<T>(value)), std::forward<Rest>(rest)...);
}

// Pure map: transforms each element of `input` via `fn`, returning a new vector.
template <typename Container, typename Fn>
auto map(const Container& input, Fn&& fn)
    -> std::vector<std::decay_t<decltype(fn(*std::begin(input)))>> {
    using OutT = std::decay_t<decltype(fn(*std::begin(input)))>;
    std::vector<OutT> result;
    result.reserve(input.size());
    std::transform(std::begin(input), std::end(input), std::back_inserter(result),
                   std::forward<Fn>(fn));
    return result;
}

// Curried overload of map for use inside fp::pipe(...).
template <typename Fn>
auto mapped(Fn&& fn) {
    return [f = std::forward<Fn>(fn)](const auto& input) {
        return map(input, f);
    };
}

// Pure filter: returns a new vector containing only elements satisfying `pred`.
template <typename T, typename Pred>
std::vector<T> filter(const std::vector<T>& input, Pred&& pred) {
    std::vector<T> result;
    result.reserve(input.size());
    std::copy_if(input.begin(), input.end(), std::back_inserter(result),
                 std::forward<Pred>(pred));
    return result;
}

// Curried overload of filter for use inside fp::pipe(...).
template <typename Pred>
auto filtered(Pred&& pred) {
    return [p = std::forward<Pred>(pred)](const auto& input) {
        return filter(input, p);
    };
}

// Pure left fold (reduce / accumulate): folds `input` from left to right without mutating `init`.
template <typename Container, typename Acc, typename BinaryOp>
Acc foldl(const Container& input, Acc init, BinaryOp&& op) {
    return std::accumulate(std::begin(input), std::end(input), std::move(init),
                           std::forward<BinaryOp>(op));
}

// Pure stable sort: returns a new sorted vector according to `lessFn`.
template <typename T, typename Compare>
std::vector<T> sortBy(const std::vector<T>& input, Compare&& lessFn) {
    std::vector<T> copy = input;
    std::stable_sort(copy.begin(), copy.end(), std::forward<Compare>(lessFn));
    return copy;
}

// Curried overload of sortBy for use inside fp::pipe(...).
template <typename Compare>
auto sortedBy(Compare&& lessFn) {
    return [cmp = std::forward<Compare>(lessFn)](const auto& input) {
        return sortBy(input, cmp);
    };
}

// Pure take: returns up to the first `n` elements as a new vector.
template <typename T>
std::vector<T> take(const std::vector<T>& input, std::size_t n) {
    const std::size_t count = (std::min)(n, input.size());
    return std::vector<T>(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(count));
}

inline auto taken(std::size_t n) {
    return [n](const auto& input) {
        return take(input, n);
    };
}

// Pure countIf: counts elements matching `pred`.
template <typename Container, typename Pred>
std::size_t countIf(const Container& input, Pred&& pred) {
    return static_cast<std::size_t>(
        std::count_if(std::begin(input), std::end(input), std::forward<Pred>(pred)));
}

// Pure deterministic xorshift32 step: returns next non-zero seed without global state.
constexpr uint32_t nextSeed(uint32_t seed) {
    uint32_t x = (seed == 0u) ? 0x9E3779B9u : seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return (x == 0u) ? 0x6D2B79F5u : x;
}

template <typename T>
struct RngPick {
    T value;
    uint32_t nextSeed;
};

// Pure selection from a non-empty std::array using functional PRNG threading.
template <typename T, std::size_t N>
constexpr RngPick<T> pickFrom(uint32_t seed, const std::array<T, N>& choices) {
    static_assert(N > 0, "choices array must not be empty");
    const uint32_t s1 = nextSeed(seed);
    const std::size_t idx = static_cast<std::size_t>(s1 % N);
    return RngPick<T>{choices[idx], s1};
}

}  // namespace fp
