#pragma once

#include <string>
#include <string_view>
#include <type_traits>

namespace injamm::sqlite3 {

// 実行時にキー文字列でフィールドへアクセスできる型（sqlite3 の行ビュー等）
template <class T>
concept runtime_field_accessible = requires(T const& t, std::string_view key) {
  { t.find(key) } -> std::same_as<std::string>;
};

// 前方イテレータを備え、要素型 value_type を持つコンテナ型（結果セット等）
template <class T>
concept forward_iterable = requires(T& t) {
  typename T::value_type;
  { t.begin() };
  { t.end() };
};

} // namespace injamm::sqlite3
