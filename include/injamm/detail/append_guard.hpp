#pragma once

#include "../config.hpp"
#include <cstddef>
#include <string>
#include <string_view>

namespace injamm::detail {

/**
 * @brief hot path 用の容量確認付き append helper。
 * @details INJAMM_HAS_EXCEPTIONS 時は通常の append を行い、allocation failure は境界 catch に委ねる。
 *          !INJAMM_HAS_EXCEPTIONS 時は事前に残容量を確認し、不足時は false を返すことで trap を避ける。
 */
#if INJAMM_HAS_EXCEPTIONS
template <class Buffer>
constexpr bool hot_append(Buffer& out, std::string_view sv) {
  out.append(sv);
  return true;
}

template <class Buffer>
constexpr bool hot_append(Buffer& out, const char* data, std::size_t n) {
  out.append(data, n);
  return true;
}

template <class Buffer>
constexpr bool hot_append(Buffer& out, std::string const& src, std::size_t pos, std::size_t n) {
  out.append(src, pos, n);
  return true;
}

template <class Buffer>
constexpr bool hot_append(Buffer& out, std::size_t n, char c) {
  out.append(n, c);
  return true;
}
#else
template <class Buffer>
constexpr bool hot_append(Buffer& out, std::string_view sv) noexcept {
  if (sv.size() > out.capacity() - out.size()) return false;
  out.append(sv);
  return true;
}

template <class Buffer>
constexpr bool hot_append(Buffer& out, const char* data, std::size_t n) noexcept {
  if (static_cast<std::size_t>(n) > out.capacity() - out.size()) return false;
  out.append(data, n);
  return true;
}

template <class Buffer>
constexpr bool hot_append(Buffer& out, std::string const& src, std::size_t pos, std::size_t n) noexcept {
  if (n > out.capacity() - out.size()) return false;
  out.append(src, pos, n);
  return true;
}

template <class Buffer>
constexpr bool hot_append(Buffer& out, std::size_t n, char c) noexcept {
  if (n > out.capacity() - out.size()) return false;
  out.append(n, c);
  return true;
}
#endif

} // namespace injamm::detail