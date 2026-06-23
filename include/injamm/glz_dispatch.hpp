#pragma once

#include "serialize_value.hpp"
#include <glaze/glaze.hpp>
#include <string_view>
#include <tuple>
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

// -- bind() 用コンテナ直接バインド型 --

/**
 * @brief NTTP 文字列から string_view を取得するヘルパ（bound_context 専用）
 *
 * @details fixed_string（.data メンバ）と FrozenString（.data() メソッド）の
 *          両方に対応する。escape_hatch.hpp の nttp_string_view と同等だが
 *          include 順序の問題を避けるため glz_dispatch.hpp 内に複製している。
 */
template <typename S>
constexpr std::string_view bc_nttp_sv(S const& s) noexcept {
  if constexpr (requires { s.data; }) {
    return {s.data, s.size()};
  } else {
    return {s.data(), s.size()};
  }
}

/**
 * @brief NTTP 文字列のリストを単一型に包むコンパイル時型
 *
 * @details bound_context のキーリストとして使用するタグ型。
 *          sizeof...(Names) 個の文字列を keys 配列として静的に保持する。
 *
 * @tparam Names バインド変数名の NTTP パック（auto）
 */
template <auto... Names>
struct name_list {
  static constexpr std::size_t size = sizeof...(Names);
  static constexpr std::array<std::string_view, size> keys = {
    bc_nttp_sv(Names)...
  };
};

/**
 * @brief コンテナ直接バインド用コンテキスト型
 *
 * @details NTTP 名で複数のコンテナ（または任意の型）をバインドし、
 *          injamm::render の第二引数として渡すためのコンテキスト型。
 *          refs は参照を保持するため、元コンテナの生存期間内で使用すること。
 *
 * @tparam NameList  name_list<Names...> 型
 * @tparam Containers バインドするコンテナ型パック
 */
template <typename NameList, typename... Containers>
struct bound_context {
  /** @brief glz::reflect による自動反映を無効化（専用の特殊化を使用） */
  static constexpr bool glaze_reflect = false;
  /** @brief 各コンテナへの const 参照を保持するタプル */
  std::tuple<Containers const&...> refs;
};

} // namespace injamm::detail

namespace glz {

/**
 * @brief bound_context 用 reflect 特殊化
 *
 * @details name_list<Names...> のキー配列と要素数を提供することで、
 *          injamm::detail の ct_glz_reflectable コンセプトを満足させる。
 *          bound_context を injamm::render の T として使用可能にする。
 *
 * @tparam Names      バインド変数名パック（auto NTTP）
 * @tparam Containers バインドコンテナ型パック
 */
template <auto... Names, typename... Containers>
struct reflect<
  injamm::detail::bound_context<
    injamm::detail::name_list<Names...>,
    Containers...
  >> {
  static constexpr auto size = sizeof...(Names);
  static constexpr std::array<std::string_view, size> keys = {
    injamm::detail::bc_nttp_sv(Names)...
  };
};

/**
 * @brief bound_context 用 to_tie 特殊化
 *
 * @details refs タプル（std::tuple<Containers const&...>）を
 *          glz::get<I> でアクセス可能な glz::tuple に変換して返す。
 *          bytecode_exec の dispatch_field から呼び出される。
 *
 * @tparam Names      バインド変数名パック（auto NTTP）
 * @tparam Containers バインドコンテナ型パック
 * @param  ctx        bound_context インスタンス
 * @return glz::tuple<Containers const&...>
 */
template <auto... Names, typename... Containers>
auto to_tie(
  injamm::detail::bound_context<
    injamm::detail::name_list<Names...>,
    Containers...
  > const& ctx
) -> glz::tuple<Containers const&...> {
  return std::apply([](Containers const&... refs) {
    return glz::tie(refs...);
  }, ctx.refs);
}

} // namespace glz
