#pragma once

#include <string>
#include <string_view>
#include <type_traits>

namespace injamm::sqlite3 {

template <class T>
concept runtime_field_accessible = requires(T const& t, std::string_view key) {
  { t.find(key) } -> std::same_as<std::string>;
};

template <class T>
concept forward_iterable = requires(T& t) {
  typename T::value_type;
  { t.begin() };
  { t.end() };
};

} // namespace injamm::sqlite3
