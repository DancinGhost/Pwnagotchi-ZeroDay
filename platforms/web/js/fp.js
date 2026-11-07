// Pure functional combinators & stateless xorshift32 PRNG (ported from include/fp.hpp).
(function (root) {
  'use strict';

  function clampVal(value, low, high) {
    return value < low ? low : (high < value ? high : value);
  }

  function pipe(value) {
    let acc = value;
    for (let i = 1; i < arguments.length; i++) {
      acc = arguments[i](acc);
    }
    return acc;
  }

  function map(input, fn) {
    const out = new Array(input.length);
    for (let i = 0; i < input.length; i++) {
      out[i] = fn(input[i], i);
    }
    return out;
  }

  function mapped(fn) {
    return function (input) {
      return map(input, fn);
    };
  }

  function filter(input, pred) {
    const out = [];
    for (let i = 0; i < input.length; i++) {
      if (pred(input[i], i)) {
        out.push(input[i]);
      }
    }
    return out;
  }

  function filtered(pred) {
    return function (input) {
      return filter(input, pred);
    };
  }

  function foldl(input, init, op) {
    let acc = init;
    for (let i = 0; i < input.length; i++) {
      acc = op(acc, input[i], i);
    }
    return acc;
  }

  // Stable sort taking a C++-style strict-weak-ordering boolean comparator lessFn(a, b).
  function sortBy(input, lessFn) {
    return input.slice().sort(function (a, b) {
      if (lessFn(a, b)) return -1;
      if (lessFn(b, a)) return 1;
      return 0;
    });
  }

  function sortedBy(lessFn) {
    return function (input) {
      return sortBy(input, lessFn);
    };
  }

  function take(input, n) {
    const count = Math.max(0, Math.min(n, input.length));
    return input.slice(0, count);
  }

  function taken(n) {
    return function (input) {
      return take(input, n);
    };
  }

  function countIf(input, pred) {
    let count = 0;
    for (let i = 0; i < input.length; i++) {
      if (pred(input[i], i)) count++;
    }
    return count;
  }

  // Exact unsigned 32-bit xorshift32 matching include/fp.hpp bit-for-bit.
  function nextSeed(seed) {
    let x = (seed >>> 0) === 0 ? 0x9E3779B9 : (seed >>> 0);
    x = (x ^ ((x << 13) >>> 0)) >>> 0;
    x = (x ^ (x >>> 17)) >>> 0;
    x = (x ^ ((x << 5) >>> 0)) >>> 0;
    return x === 0 ? 0x6D2B79F5 : x;
  }

  function pickFrom(seed, choices) {
    if (!choices || choices.length === 0) {
      throw new Error('choices array must not be empty');
    }
    const s1 = nextSeed(seed);
    const idx = s1 % choices.length;
    return { value: choices[idx], nextSeed: s1 };
  }

  const fp = Object.freeze({
    clampVal,
    pipe,
    map,
    mapped,
    filter,
    filtered,
    foldl,
    sortBy,
    sortedBy,
    take,
    taken,
    countIf,
    nextSeed,
    pickFrom
  });

  root.ZD = root.ZD || {};
  root.ZD.fp = fp;

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = fp;
  }
})(typeof globalThis !== 'undefined' ? globalThis : window);
