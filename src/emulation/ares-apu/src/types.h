#pragma once

// Minimal shim for nall types used by the SNES APU emulation.
// Provides Natural<N>, Integer<N>, Boolean, and related utilities.

#include <cstdint>
#include <cmath>
#include <cstring>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using f32 = float;
using f64 = double;

namespace Math {
  static constexpr long double Pi = 3.14159265358979323846L;
}

// Signed clamp to N bits: clamps to [-(2^(N-1)), 2^(N-1)-1]
template<u32 bits> inline auto sclamp(s64 x) -> s64 {
  constexpr s64 b = s64(1) << (bits - 1);
  constexpr s64 m = b - 1;
  return (x > m) ? m : (x < -b) ? -b : x;
}

// Forward declarations
template<u32 Precision> struct Natural;
template<u32 Precision> struct Integer;

// BitReference: allows data.bit(n) = value and reading
template<typename T>
struct BitReference {
  T& target;
  u32 lo;
  u32 hi;

  BitReference(T& t, u32 index) : target(t), lo(index), hi(index) {}
  BitReference(T& t, u32 lo_, u32 hi_) : target(t), lo(lo_), hi(hi_) {}

  operator u64() const {
    u64 mask = ((u64(1) << (hi - lo + 1)) - 1);
    return (target.rawValue() >> lo) & mask;
  }

  auto operator=(u64 value) -> BitReference& {
    u64 bits = hi - lo + 1;
    u64 mask = ((u64(1) << bits) - 1);
    u64 raw = target.rawValue();
    raw = (raw & ~(mask << lo)) | ((value & mask) << lo);
    target.setRaw(raw);
    return *this;
  }

  auto operator^=(u64 value) -> BitReference& {
    *this = (u64)*this ^ value;
    return *this;
  }

  auto operator&=(u64 value) -> BitReference& {
    *this = (u64)*this & value;
    return *this;
  }

  auto operator|=(u64 value) -> BitReference& {
    *this = (u64)*this | value;
    return *this;
  }
};

// Natural<N>: unsigned integer masked to N bits
template<u32 Precision>
struct Natural {
  static constexpr u64 mask = Precision < 64 ? (u64(1) << Precision) - 1 : ~u64(0);
  u64 data = 0;

  Natural() = default;
  template<typename T> Natural(const T& value) : data(u64(value) & mask) {}

  auto rawValue() const -> u64 { return data; }
  auto setRaw(u64 v) -> void { data = v & mask; }

  operator u64() const { return data; }

  template<typename T> auto& operator=(const T& value) { data = u64(value) & mask; return *this; }
  template<typename T> auto& operator+=(const T& value) { data = (data + u64(value)) & mask; return *this; }
  template<typename T> auto& operator-=(const T& value) { data = (data - u64(value)) & mask; return *this; }
  template<typename T> auto& operator*=(const T& value) { data = (data * u64(value)) & mask; return *this; }
  template<typename T> auto& operator/=(const T& value) { data = (data / u64(value)) & mask; return *this; }
  template<typename T> auto& operator%=(const T& value) { data = (data % u64(value)) & mask; return *this; }
  template<typename T> auto& operator&=(const T& value) { data &= u64(value); return *this; }
  template<typename T> auto& operator|=(const T& value) { data = (data | u64(value)) & mask; return *this; }
  template<typename T> auto& operator^=(const T& value) { data = (data ^ u64(value)) & mask; return *this; }
  template<typename T> auto& operator<<=(const T& value) { data = (data << u64(value)) & mask; return *this; }
  template<typename T> auto& operator>>=(const T& value) { data >>= u64(value); return *this; }

  auto& operator++() { data = (data + 1) & mask; return *this; }
  auto operator++(int) { auto r = *this; data = (data + 1) & mask; return r; }
  auto& operator--() { data = (data - 1) & mask; return *this; }
  auto operator--(int) { auto r = *this; data = (data - 1) & mask; return r; }

  auto bit(s32 index) -> BitReference<Natural> { return {*this, (u32)index}; }
  auto bit(s32 index) const -> u64 { return (data >> index) & 1; }
  auto bit(s32 lo, s32 hi) -> BitReference<Natural> { return {*this, (u32)lo, (u32)hi}; }
  auto bit(s32 lo, s32 hi) const -> u64 {
    u64 bits = hi - lo + 1;
    return (data >> lo) & ((u64(1) << bits) - 1);
  }

  auto byte(s32 index) -> BitReference<Natural> { return {*this, (u32)(index * 8), (u32)(index * 8 + 7)}; }
  auto byte(s32 index) const -> u64 { return (data >> (index * 8)) & 0xff; }
};

// Integer<N>: signed integer masked to N bits with sign extension
template<u32 Precision>
struct Integer {
  static constexpr u64 mask = Precision < 64 ? (u64(1) << Precision) - 1 : ~u64(0);
  static constexpr u64 sign = u64(1) << (Precision - 1);
  s64 data = 0;

  static auto cast(s64 value) -> s64 {
    u64 v = u64(value) & mask;
    return s64((v ^ sign) - sign);
  }

  Integer() = default;
  template<typename T> Integer(const T& value) : data(cast(s64(value))) {}

  auto rawValue() const -> u64 { return u64(data) & mask; }
  auto setRaw(u64 v) -> void { data = cast(s64(v)); }

  operator s64() const { return data; }

  template<typename T> auto& operator=(const T& value) { data = cast(s64(value)); return *this; }
  template<typename T> auto& operator+=(const T& value) { data = cast(data + s64(value)); return *this; }
  template<typename T> auto& operator-=(const T& value) { data = cast(data - s64(value)); return *this; }
  template<typename T> auto& operator*=(const T& value) { data = cast(data * s64(value)); return *this; }
  template<typename T> auto& operator&=(const T& value) { data = cast(data & s64(value)); return *this; }
  template<typename T> auto& operator|=(const T& value) { data = cast(data | s64(value)); return *this; }
  template<typename T> auto& operator^=(const T& value) { data = cast(data ^ s64(value)); return *this; }
  template<typename T> auto& operator<<=(const T& value) { data = cast(data << s64(value)); return *this; }
  template<typename T> auto& operator>>=(const T& value) { data = cast(data >> s64(value)); return *this; }

  auto& operator++() { data = cast(data + 1); return *this; }
  auto operator++(int) { auto r = *this; data = cast(data + 1); return r; }
  auto& operator--() { data = cast(data - 1); return *this; }
  auto operator--(int) { auto r = *this; data = cast(data - 1); return r; }

  auto bit(s32 index) -> BitReference<Integer> { return {*this, (u32)index}; }
  auto bit(s32 index) const -> u64 { return (rawValue() >> index) & 1; }
  auto bit(s32 lo, s32 hi) -> BitReference<Integer> { return {*this, (u32)lo, (u32)hi}; }
  auto bit(s32 lo, s32 hi) const -> u64 {
    u64 bits = hi - lo + 1;
    return (rawValue() >> lo) & ((u64(1) << bits) - 1);
  }

  auto byte(s32 index) -> BitReference<Integer> { return {*this, (u32)(index * 8), (u32)(index * 8 + 7)}; }
  auto byte(s32 index) const -> u64 { return (rawValue() >> (index * 8)) & 0xff; }
};

// Boolean (b1): edge-detecting boolean
struct Boolean {
  bool data = false;

  Boolean() = default;
  Boolean(bool v) : data(v) {}

  operator bool() const { return data; }
  auto& operator=(bool v) { data = v; return *this; }

  // raise(value): sets to 1 if value is true; returns true if transitioned 0->1
  auto raise(bool value) -> bool {
    bool result = !data && value;
    data = value;
    return result;
  }

  // lower(value): sets to 0 if value is false; returns true if transitioned 1->0
  auto lower(bool value) -> bool {
    bool result = data && !value;
    data = value;
    return result;
  }

  auto raise() -> bool { return raise(true); }
  auto lower() -> bool { return lower(false); }
};

// Type aliases matching ares conventions
using n1  = Natural<1>;
using n2  = Natural<2>;
using n3  = Natural<3>;
using n4  = Natural<4>;
using n5  = Natural<5>;
using n7  = Natural<7>;
using n8  = Natural<8>;
using n11 = Natural<11>;
using n13 = Natural<13>;
using n14 = Natural<14>;
using n15 = Natural<15>;
using n16 = Natural<16>;

using i8  = Integer<8>;
using i16 = Integer<16>;
using i17 = Integer<17>;
using i32 = Integer<32>;

using b1 = Boolean;

// Endian macro: on little-endian, order_lsb2(a,b) = a,b
// We assume little-endian (x86/ARM in LE mode)
#define order_lsb2(a,b) a,b

// Range iterator for "for(u32 n : range(8))" syntax
struct range_t {
  struct iterator {
    s64 pos;
    auto operator*() const -> s64 { return pos; }
    auto operator!=(const iterator& rhs) const -> bool { return pos != rhs.pos; }
    auto& operator++() { ++pos; return *this; }
  };
  s64 start, stop;
  auto begin() const -> iterator { return {start}; }
  auto end()   const -> iterator { return {stop}; }
};

inline auto range(s64 size) -> range_t { return {0, size}; }
inline auto range(s64 start, s64 size) -> range_t { return {start, size}; }
