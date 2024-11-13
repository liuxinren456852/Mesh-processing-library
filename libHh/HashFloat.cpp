// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "libHh/HashFloat.h"

namespace hh {

// Idea:
// Each bucket will contain a unique representative floating point number.
// When entering a number into an empty bucket, its neighbors are tested first; if one of its neighbors
//  already has a FP number, the new bucket inherits this number.
// -> equivalence relation is correct.
// Small numbers (abs(x) < threshold) are placed in a special bucket that is
// adjacent to numbers that are almost small.

// TODO: Exploit the fact that "Adjacent floats (of the same sign) have adjacent integer representations".
//  See https://randomascii.wordpress.com/2012/01/23/stupid-float-tricks-2/

namespace {

constexpr uint32_t k_small_key = 1;
constexpr float k_small_val = 1e-30f;

inline float compute_factor(int n) { return 1.f + pow(.5f, 23.f - n) * .49999f; }

inline uint32_t float_bits_to_unsigned(const float& f) {
  union {
    uint32_t ui;
    float f;
  } u;
  u.f = f;
  return u.ui;
}

}  // namespace

HashFloat::HashFloat(int nignorebits, float small) : _nignorebits(nignorebits), _small(small) {
  // use float to detect override with "0"
  _nignorebits = getenv_int("HASHFLOAT_NIGNOREBITS", _nignorebits, true);
  assertx(_nignorebits >= 0 && _nignorebits <= 22);
  _small = getenv_float("HASHFLOAT_SMALL", _small, true);
  _factor = compute_factor(_nignorebits);
  _recip = 1.f / _factor;
}

// Return a key that encodes the bucket in which the value lies.
inline uint32_t HashFloat::encode(float f) const {
  if (abs(f) <= _small) {
    return k_small_key;
  } else {
    uint32_t u = float_bits_to_unsigned(f);
    return assertx(u >> _nignorebits);
  }
}

float HashFloat::enter(float f) {
  bool foundexact = false;
  uint32_t bucketn = encode(f);
  float r = _m.retrieve(bucketn);  // retrieve closest float
  if (r) foundexact = true;
  if (0) {
    SHOW(encode(f / _factor / _factor));
    SHOW(encode(f / _factor));
    SHOW(encode(f));
    SHOW(encode(f * _factor));
    SHOW(encode(f * _factor * _factor));
  }
  if (!r) r = _m.retrieve(encode(f * _factor));
  if (!r) r = _m.retrieve(encode(f / _factor));
  if (!r) r = _m.retrieve(encode(f * _factor * _factor));
  if (!r) r = _m.retrieve(encode(f / _factor / _factor));
  if (r) {  // found
    // if found in adjacent cell, propagate close value here
    if (!foundexact) _m.enter(bucketn, r);
    if (r == k_small_val) r = 0.f;
    return r;
  }
  float fe = f;
  if (bucketn == k_small_key) {
    fe = k_small_val;
    f = 0.f;
  }
  _m.enter(bucketn, fe);
  return f;
}

void HashFloat::pre_consider(float f) {
  uint32_t bc = encode(f);
  float r = _m.retrieve(bc);  // retrieve closest float
  if (r) return;
  uint32_t bp = encode(f * _factor);
  if (bp == bc) bp = encode(f * _factor * _factor);
  uint32_t bm = encode(f * _recip);
  if (bm == bc) bm = encode(f * _recip * _recip);
  float rp = _m.retrieve(bp);
  float rm = _m.retrieve(bm);
  if (rp && rm) {
    Warning("HashFloat: Performing unification");
    assertx(bc != k_small_key);  // That case has not yet been considered.
    _m.enter(bc, f);
    for (float ff = f;;) {
      ff *= _factor;
      uint32_t b = encode(ff);
      assertx(b != k_small_key);
      if (!_m.retrieve(b)) break;
      _m.replace(b, f);
    }
    for (float ff = f;;) {
      ff *= _recip;
      uint32_t b = encode(ff);
      assertx(b != k_small_key);
      if (!_m.retrieve(b)) break;
      _m.replace(b, f);
    }
  } else if (rp) {
    _m.enter(bc, rp);
  } else if (rm) {
    _m.enter(bc, rm);
  } else {
    _m.enter(bc, bc == k_small_key ? k_small_val : f);
  }
}

}  // namespace hh
