#pragma once

#include "serialize_value.hpp"
#include <glaze/glaze.hpp>
#include <string_view>
#include <type_traits>
#include <utility>

namespace injamm::detail {

// -- Moved from ct_render.hpp --

template <class T>
concept ct_is_vector_like =
    !std::is_arithmetic_v<T> &&
    !std::same_as<T, std::string> &&
    !std::same_as<T, std::string_view> &&
    requires(T const& v) {
      typename T::value_type;
      { v.size() } -> std::convertible_to<std::size_t>;
      { v[0] };
      { v.begin() };
      { v.end() };
    };

template <class T>
concept ct_is_map_like = requires {
  typename T::key_type;
  typename T::mapped_type;
  typename T::value_type;
  { std::declval<T const&>().size() } -> std::convertible_to<std::size_t>;
  { std::declval<T const&>().begin() };
  { std::declval<T const&>().end() };
};

template <class T>
concept ct_is_set_like =
    !std::is_arithmetic_v<T> &&
    !std::same_as<T, std::string> &&
    !std::same_as<T, std::string_view> &&
    requires(T const& v) {
      typename T::value_type;
      { v.size() } -> std::convertible_to<std::size_t>;
      { v.begin() };
      { v.end() };
    } && !requires(T const& v, std::size_t i) {
      { v[i] };
    } && !requires {
      typename T::key_type;
      typename T::mapped_type;
    };

template <class T>
concept ct_glz_reflectable = requires {
  glz::reflect<T>::size;
} && !is_std_map_like_v<T>;

} // namespace injamm::detail
