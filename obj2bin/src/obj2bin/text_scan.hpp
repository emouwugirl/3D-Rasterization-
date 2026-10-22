// Text scanning shared by the OBJ and PLY readers: lines, words and fast, locale-independent number
// parsing (internal header).
#pragma once

#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace rasty::convert::detail {

// Horizontal whitespace; '\n' is handled by the line loops.
inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }
inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

inline void skip_spaces(const char*& p, const char* end) {
  while (p < end && is_space(*p)) ++p;
}

// The line that starts at `pos`, without its terminator ('\n', and a '\r' before it); moves `pos` to
// the start of the next line. Returns false when `pos` is at the end of the text.
inline bool next_line(std::string_view text, std::size_t& pos, std::string_view& line) {
  if (pos >= text.size()) return false;
  const std::size_t nl = text.find('\n', pos);
  const std::size_t line_end = nl == std::string_view::npos ? text.size() : nl;
  line = text.substr(pos, line_end - pos);
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  pos = nl == std::string_view::npos ? text.size() : nl + 1;
  return true;
}

// The next whitespace-separated word (empty at the end of the text); moves p past it.
inline std::string_view next_token(const char*& p, const char* end) {
  skip_spaces(p, end);
  const char* s = p;
  while (p < end && !is_space(*p)) ++p;
  return std::string_view(s, static_cast<std::size_t>(p - s));
}

// double -> float. Values beyond float's range become +-infinity; a plain cast of them is undefined
// behaviour in C++.
inline float to_float(double d) {
  constexpr double kMax = static_cast<double>(std::numeric_limits<float>::max());
  if (d > kMax) return std::numeric_limits<float>::infinity();
  if (d < -kMax) return -std::numeric_limits<float>::infinity();
  if (d != d) return std::numeric_limits<float>::quiet_NaN();
  return static_cast<float>(d);
}

// [+-]digits. On failure returns false and leaves p unchanged.
inline bool parse_int(const char*& p, const char* end, int64_t& out) {
  const char* s = p;
  bool neg = false;
  if (s < end && (*s == '+' || *s == '-')) {
    neg = *s == '-';
    ++s;
  }
  if (s >= end || !is_digit(*s)) return false;
  int64_t v = 0;
  while (s < end && is_digit(*s)) {
    if (v > (INT64_MAX - 9) / 10) return false;
    v = v * 10 + (*s - '0');
    ++s;
  }
  out = neg ? -v : v;
  p = s;
  return true;
}

// strtod on a copy of the whole token (the input is not necessarily NUL-terminated). Handles inf/nan
// and numbers the fast path declines. strtod expects the decimal point of the current C locale, so the
// token's '.' is rewritten into that form first; the result is the same in every locale.
inline bool parse_double_slow(const char*& p, const char* end, double& out) {
  std::size_t n = 0;
  while (p + n < end && !is_space(p[n]) && p[n] != '\n' && p[n] != '/') ++n;
  std::string token(p, n);
  const char* point = std::localeconv()->decimal_point;
  const std::size_t dot = token.find('.');
  std::size_t extra = 0;  // how much longer the locale's decimal point is than '.'
  if (dot != std::string::npos && point && point[0] != '\0' && std::strcmp(point, ".") != 0) {
    token.replace(dot, 1, point);
    extra = std::strlen(point) - 1;
  }
  char* stop = nullptr;
  const double v = std::strtod(token.c_str(), &stop);
  std::size_t used = static_cast<std::size_t>(stop - token.c_str());
  if (used == 0) return false;
  if (dot != std::string::npos && used > dot) used -= extra;
  out = v;
  p += used;
  return true;
}

// Skips a UTF-8 byte order mark at the start of a text file.
inline void skip_utf8_bom(const char*& p, const char* end) {
  if (end - p >= 3 && static_cast<unsigned char>(p[0]) == 0xEF && static_cast<unsigned char>(p[1]) == 0xBB &&
      static_cast<unsigned char>(p[2]) == 0xBF) {
    p += 3;
  }
}

// [+-]digits[.digits][(e|E)[+-]digits]. Numbers with at most 19 significant digits and a decimal
// exponent within +-22 (all numbers in typical model files) are converted exactly with one rounding;
// anything else falls back to strtod. On failure returns false and leaves p unchanged.
inline bool parse_double(const char*& p, const char* end, double& out) {
  static const double kPow10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                  1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
  const char* s = p;
  bool neg = false;
  if (s < end && (*s == '+' || *s == '-')) {
    neg = *s == '-';
    ++s;
  }
  uint64_t mant = 0;
  int digits = 0;  // significant digits accumulated in mant
  int exp10 = 0;
  bool any = false;
  bool inexact = false;
  while (s < end && is_digit(*s)) {
    any = true;
    if (digits < 19) {
      mant = mant * 10 + static_cast<uint64_t>(*s - '0');
      if (mant != 0) ++digits;
    } else {
      ++exp10;
      inexact = true;
    }
    ++s;
  }
  if (s < end && *s == '.') {
    ++s;
    while (s < end && is_digit(*s)) {
      any = true;
      if (digits < 19) {
        mant = mant * 10 + static_cast<uint64_t>(*s - '0');
        if (mant != 0) ++digits;
        --exp10;
      } else {
        inexact = true;
      }
      ++s;
    }
  }
  if (!any) return parse_double_slow(p, end, out);  // "inf", "nan", or not a number
  if (s < end && (*s == 'e' || *s == 'E')) {
    const char* e = s + 1;
    bool eneg = false;
    if (e < end && (*e == '+' || *e == '-')) {
      eneg = *e == '-';
      ++e;
    }
    if (e < end && is_digit(*e)) {
      int ev = 0;
      while (e < end && is_digit(*e)) {
        if (ev < 100000) ev = ev * 10 + (*e - '0');
        ++e;
      }
      exp10 += eneg ? -ev : ev;
      s = e;
    }
  }
  double v;
  if (mant == 0) {
    v = 0.0;
  } else if (!inexact && mant < (1ull << 53) && exp10 >= -22 && exp10 <= 22) {
    v = static_cast<double>(mant);
    v = exp10 < 0 ? v / kPow10[-exp10] : v * kPow10[exp10];
  } else {
    return parse_double_slow(p, end, out);
  }
  out = neg ? -v : v;
  p = s;
  return true;
}

inline bool parse_float(const char*& p, const char* end, float& out) {
  double d;
  if (!parse_double(p, end, d)) return false;
  out = to_float(d);
  return true;
}

}  // namespace rasty::convert::detail
