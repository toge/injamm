#pragma once

/**
 * @file nttp_render.hpp
 * @brief NTTP コンパイル時レンダリング公開 API — render / render_partial
 * @details escape_hatch.hpp から分割。detail/nttp_data.hpp のキャッシュ層と
 *          bytecode_exec / ct_exec を組み合わせて公開 API を提供。
 */

#include <algorithm>
#include <concepts>
#include <string>
#include <string_view>
#include <expected>

#include "types.hpp"
#include "detail/nttp_data.hpp"
#include "bytecode_exec.hpp"
#include "ct_exec.hpp"
#include "engine.hpp"  // bind() は engine.hpp に定義、NTTP render でも使うため再エクスポート

#if __has_include(<frozenchars/mod/core.hpp>)
#include <frozenchars/mod/core.hpp>
#ifndef INJAMM_HAS_FROZENCHARS
#define INJAMM_HAS_FROZENCHARS 1
#endif
#endif

namespace injamm {

/**
 * @brief NTTP ベースのレンダリング（真のコンパイル時パース、SoA 版）
 *
 * @details テンプレート引数 Tmpl で渡された文字列をコンパイル時にパースし、
 *          実行時には変数値の埋め込みのみを行う。
 *          {{var}} は HTML エスケープ付き、{{{var}}} は生出力。
 *          セクション / if / @変数 / フィルター / break-continue に対応。
 *
 * @tparam Tmpl コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam T    コンテキスト値の型（glz::meta<T> 要特殊化）
 * @param value コンテキスト値の const 参照
 * @return expected<std::string> レンダリング結果、またはエラー（error_ctx）
 */
template <fixed_string Tmpl, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T>
[[nodiscard]] expected<std::string> render(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  if constexpr (detail::ct_is_unrollable(D::ct_bc))
    return detail::ct_executor<D, T>::run(value);
  else if constexpr (detail::ct_is_hybrid_eligible(D::ct_bc)) {
    auto const&                rbc  = detail::nttp_partial_bytecode_holder<D, T>();
    std::string                out;
    detail::bc_executor<T>     exec(rbc, value, value, nullptr, out);
    auto                       r = detail::ct_hybrid_executor<D, T>::run_into(value, out, exec);
    if (!r)
      return std::unexpected(r.error());
    return out;
  } else
    return detail::bc_execute(detail::nttp_partial_bytecode_holder<D, T>(), value);
}

/**
 * @brief NTTP ベースのレンダリング（外部 partial レジストリ指定版）
 *
 * @details 第2テンプレート引数に ct_partials<...> で定義した共有レジストリを渡す。
 *          テンプレート内の {{> name}} はレジストリから解決される（call_partial）。
 *          同じ ct_partials 型を複数の render 呼び出しに渡すことで共有できる。
 *
 * @tparam Tmpl コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam Reg  ct_partials<"name","body",...> 型（外部 partial レジストリ）
 * @tparam T    コンテキスト値の型（glz::meta<T> 要特殊化）
 * @param value コンテキスト値の const 参照
 * @return expected<std::string> レンダリング結果、またはエラー（error_ctx）
 */
template <fixed_string Tmpl, typename Reg, bool TrimBlocks = false, bool LstripBlocks = false, typename T>
  requires detail::is_ct_partials_v<Reg>
[[nodiscard]] expected<std::string> render(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T, Reg>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  if constexpr (detail::ct_is_unrollable(D::ct_bc))
    return detail::ct_executor<D, T>::run(value);
  else if constexpr (detail::ct_is_hybrid_eligible(D::ct_bc)) {
    auto const&                rbc  = detail::nttp_partial_bytecode_holder<D, T>();
    std::string                out;
    detail::bc_executor<T>     exec(rbc, value, value, nullptr, out);
    auto                       r = detail::ct_hybrid_executor<D, T>::run_into(value, out, exec);
    if (!r)
      return std::unexpected(r.error());
    return out;
  } else
    return detail::bc_execute(detail::nttp_partial_bytecode_holder<D, T>(), value);
}

/**
 * @brief NTTP ベースのレンダリング（バッファ再利用版）
 *
 * @details render() のバッファ再利用オーバーロード。
 *          既存の std::string インスタンスを出力先として受け取り、
 *          内部バッファを再利用することでアロケーションを削減する。
 *          出力文字列の内容はクリアされる。
 *
 * @tparam Tmpl コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam T    コンテキスト値の型（glz::meta<T> 要特殊化）
 * @param value コンテキスト値の const 参照
 * @param out   出力先バッファ（内容はクリアされる）
 * @return expected<void> 実行結果、またはエラー（error_ctx）
 */
template <fixed_string Tmpl, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T>
[[nodiscard]] expected<void> render(T const& value, std::string& out) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  if constexpr (detail::ct_is_unrollable(D::ct_bc)) {
    detail::ct_executor<D, T>::run_into(value, out);
    return {};
  } else if constexpr (detail::ct_is_hybrid_eligible(D::ct_bc)) {
    auto const&                rbc  = detail::nttp_partial_bytecode_holder<D, T>();
    detail::bc_executor<T>     exec(rbc, value, value, nullptr, out);
    return detail::ct_hybrid_executor<D, T>::run_into(value, out, exec);
  } else
    return detail::bc_execute_into(detail::nttp_partial_bytecode_holder<D, T>(), value, out);
}

/**
 * @brief NTTP ベースのレンダリング（ストリーミング sink 版）
 *
 * @details render() の sink 出力オーバーロード。出力全体を std::string に
 *          構築せず断片単位で sink に書き出す。callback_sink<Fn> を渡すと
 *          コールバック関数で断片を受け取れる。
 *
 * @tparam Tmpl コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam Sink detail::output_sink を満たす型（std::string は除外）
 * @param value コンテキスト値の const 参照
 * @param sink  出力先 sink
 * @return expected<void> 実行結果、またはエラー
 */
template <fixed_string Tmpl, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T, typename Sink>
  requires detail::output_sink<Sink> && (!std::same_as<std::remove_cvref_t<Sink>, std::string>)
[[nodiscard]] expected<void> render(T const& value, Sink& sink) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into_sink(detail::nttp_partial_bytecode_holder<D, T>(), value, sink);
}

/**
 * @brief NTTP ベースのレンダリング（@var 定数展開版）
 *
 * @details テンプレート引数 Tmpl で渡された文字列中の @var(name) を
 *          コンパイル時に定数値に展開してからパース・レンダリングする。
 *          展開後の文字列に対して {{var}} の通常レンダリングが行われる。
 *
 * @tparam Tmpl    コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam Entries キー・バリューペア（キー1, 値1, キー2, 値2, ...）
 * @tparam T       コンテキスト値の型（glz::meta<T> 要特殊化）
 * @param value    コンテキスト値の const 参照
 * @return expected<std::string> レンダリング結果、またはエラー
 */
template <fixed_string Tmpl, fixed_string... Entries, typename T>
  requires(sizeof...(Entries) > 0 && (detail::is_fixed_string_type_v<decltype(Entries)> && ...))
[[nodiscard]] expected<std::string> render(T const& value) {
  static_assert(sizeof...(Entries) % 2 == 0, "injamm: @var entries must be key-value pairs (even count). "
                                             "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data)");
  using D = detail::nttp_atvar_data<Tmpl, T, Entries...>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute(detail::nttp_bytecode_holder<D>(), value);
}

/**
 * @brief NTTP ベースのレンダリング バッファ再利用版（@var 定数展開版）
 *
 * @details render() のバッファ再利用オーバーロード。
 *          既存の std::string インスタンスを出力先として受け取り、
 *          内部バッファを再利用することでアロケーションを削減する。
 *
 * @tparam Tmpl    コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam Entries キー・バリューペア（キー1, 値1, キー2, 値2, ...）
 * @tparam T       コンテキスト値の型（glz::meta<T> 要特殊化）
 * @param value    コンテキスト値の const 参照
 * @param out      出力先バッファ（内容はクリアされる）
 * @return expected<void> 実行結果、またはエラー
 */
template <fixed_string Tmpl, fixed_string... Entries, typename T>
  requires(sizeof...(Entries) > 0 && (detail::is_fixed_string_type_v<decltype(Entries)> && ...))
[[nodiscard]] expected<void> render(T const& value, std::string& out) {
  static_assert(sizeof...(Entries) % 2 == 0, "injamm: @var entries must be key-value pairs (even count). "
                                             "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data, out)");
  using D = detail::nttp_atvar_data<Tmpl, T, Entries...>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into(detail::nttp_bytecode_holder<D>(), value, out);
}

/**
 * @brief NTTP ベースのレンダリング（@var 定数展開 + ストリーミング sink 版）
 *
 * @details @var 定数展開の結果を断片単位で sink に書き出す。
 *
 * @tparam Tmpl    コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam Entries キー・バリューペア（キー1, 値1, キー2, 値2, ...）
 * @tparam T       コンテキスト値の型
 * @tparam Sink    detail::output_sink を満たす型（std::string は除外）
 * @param value    コンテキスト値の const 参照
 * @param sink     出力先 sink
 * @return expected<void> 実行結果、またはエラー
 */
template <fixed_string Tmpl, fixed_string... Entries, typename T, typename Sink>
  requires(sizeof...(Entries) > 0 && (detail::is_fixed_string_type_v<decltype(Entries)> && ...) &&
          detail::output_sink<Sink> && !std::same_as<std::remove_cvref_t<Sink>, std::string>)
[[nodiscard]] expected<void> render(T const& value, Sink& sink) {
  static_assert(sizeof...(Entries) % 2 == 0, "injamm: @var entries must be key-value pairs (even count). "
                                             "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data, sink)");
  using D = detail::nttp_atvar_data<Tmpl, T, Entries...>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into_sink(detail::nttp_bytecode_holder<D>(), value, sink);
}

#if INJAMM_HAS_FROZENCHARS

// FrozenString 対応: Tmpl が fixed_string でない場合のみ選択（auto NTTP）
template <auto Tmpl, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)>)
[[nodiscard]] expected<std::string> render(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute(detail::nttp_partial_bytecode_holder<D, T>(), value);
}

template <auto Tmpl, typename Reg, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)> && detail::is_ct_partials_v<Reg>)
[[nodiscard]] expected<std::string> render(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T, Reg>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute(detail::nttp_partial_bytecode_holder<D, T>(), value);
}

template <auto Tmpl, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)>)
[[nodiscard]] expected<void> render(T const& value, std::string& out) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into(detail::nttp_partial_bytecode_holder<D, T>(), value, out);
}

template <auto Tmpl, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T, typename Sink>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)> && detail::output_sink<Sink> &&
            !std::same_as<std::remove_cvref_t<Sink>, std::string>)
[[nodiscard]] expected<void> render(T const& value, Sink& sink) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into_sink(detail::nttp_partial_bytecode_holder<D, T>(), value, sink);
}

template <auto Tmpl, auto... Entries, typename T>
  requires(sizeof...(Entries) > 0 && !detail::is_fixed_string_type_v<decltype(Tmpl)> &&
          (!detail::is_fixed_string_type_v<decltype(Entries)> && ...))
[[nodiscard]] expected<std::string> render(T const& value) {
  static_assert(sizeof...(Entries) % 2 == 0, "injamm: @var entries must be key-value pairs (even count). "
                                             "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data)");
  using D = detail::nttp_atvar_data<Tmpl, T, Entries...>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute(detail::nttp_bytecode_holder<D>(), value);
}

template <auto Tmpl, auto... Entries, typename T>
  requires(sizeof...(Entries) > 0 && !detail::is_fixed_string_type_v<decltype(Tmpl)> &&
          (!detail::is_fixed_string_type_v<decltype(Entries)> && ...))
[[nodiscard]] expected<void> render(T const& value, std::string& out) {
  static_assert(sizeof...(Entries) % 2 == 0, "injamm: @var entries must be key-value pairs (even count). "
                                             "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data, out)");
  using D = detail::nttp_atvar_data<Tmpl, T, Entries...>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into(detail::nttp_bytecode_holder<D>(), value, out);
}

template <auto Tmpl, auto... Entries, typename T, typename Sink>
  requires(sizeof...(Entries) > 0 && !detail::is_fixed_string_type_v<decltype(Tmpl)> &&
          (!detail::is_fixed_string_type_v<decltype(Entries)> && ...) &&
          detail::output_sink<Sink> && !std::same_as<std::remove_cvref_t<Sink>, std::string>)
[[nodiscard]] expected<void> render(T const& value, Sink& sink) {
  static_assert(sizeof...(Entries) % 2 == 0, "injamm: @var entries must be key-value pairs (even count). "
                                             "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data, sink)");
  using D = detail::nttp_atvar_data<Tmpl, T, Entries...>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into_sink(detail::nttp_bytecode_holder<D>(), value, sink);
}

#endif

/**
 * @brief NTTP ベースの名前付き partial レンダリング
 *
 * @details コンパイル時にパースされたテンプレートから、指定された名前の
 *          {{#partialdef name}}...{{/partialdef}} だけをレンダリングする。
 *          engine::render(value, partial_name) の CT 版相当。
 *
 * @tparam Tmpl コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam T    コンテキスト値の型（glz::meta<T> 要特殊化）
 * @param value       コンテキスト値の const 参照
 * @param partial_name レンダリングする partial の名前
 * @return expected<std::string> レンダリング結果、またはエラー
 */
template <fixed_string Tmpl, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T>
[[nodiscard]] expected<std::string> render_partial(T const& value, std::string_view partial_name) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  auto& bc = detail::nttp_partial_bytecode_holder<D, T>();
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  auto it = std::find_if(bc.partial_entries.begin(), bc.partial_entries.end(), [&](auto const& e) { return !e.local && e.name == partial_name; });
  if (it == bc.partial_entries.end())
    return std::unexpected(error_ctx{0, error_code::unknown_key, partial_name});
  return detail::bc_execute(*it->bc, value);
}

/**
 * @brief NTTP ベースの名前付き partial レンダリング（ストリーミング sink 版）
 *
 * @details 指定 partial のレンダリング結果を断片単位で sink に書き出す。
 *
 * @tparam Tmpl コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam T    コンテキスト値の型
 * @tparam Sink detail::output_sink を満たす型
 * @param value        コンテキスト値の const 参照
 * @param partial_name レンダリングする partial の名前
 * @param sink         出力先 sink
 * @return expected<void> 実行結果、またはエラー
 */
template <fixed_string Tmpl, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T, typename Sink>
  requires detail::output_sink<Sink>
[[nodiscard]] expected<void> render_partial(T const& value, std::string_view partial_name, Sink& sink) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  auto& bc = detail::nttp_partial_bytecode_holder<D, T>();
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  auto it = std::find_if(bc.partial_entries.begin(), bc.partial_entries.end(), [&](auto const& e) { return !e.local && e.name == partial_name; });
  if (it == bc.partial_entries.end())
    return std::unexpected(error_ctx{0, error_code::unknown_key, partial_name});
  return detail::bc_execute_into_sink(*it->bc, value, sink);
}

/**
 * @brief NTTP ベースの名前付き partial レンダリング（外部レジストリ指定版）
 *
 * @details 第2テンプレート引数に ct_partials<...> を渡し、レジストリ内の
 *          partial_name を単体レンダリングする。engine::render(value, "name") の CT 版相当。
 *
 * @tparam Tmpl コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam Reg  ct_partials<"name","body",...> 型
 * @tparam T    コンテキスト値の型（glz::meta<T> 要特殊化）
 * @param value       コンテキスト値の const 参照
 * @param partial_name レンダリングする partial の名前
 * @return expected<std::string> レンダリング結果、またはエラー
 */
template <fixed_string Tmpl, typename Reg, bool TrimBlocks = false, bool LstripBlocks = false, typename T>
  requires detail::is_ct_partials_v<Reg>
[[nodiscard]] expected<std::string> render_partial(T const& value, std::string_view partial_name) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T, Reg>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  auto& bc = detail::nttp_partial_bytecode_holder<D, T>();
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  auto it = std::find_if(bc.partial_entries.begin(), bc.partial_entries.end(), [&](auto const& e) { return !e.local && e.name == partial_name; });
  if (it == bc.partial_entries.end())
    return std::unexpected(error_ctx{0, error_code::unknown_key, partial_name});
  return detail::bc_execute(*it->bc, value);
}

/**
 * @brief NTTP ベースの名前付き partial レンダリング（partial 名をテンプレート引数で指定）
 *
 * @details レンダリングする partial 名をテンプレート引数 PartialName で指定する。
 *          指定 partial とそれが推移的に参照する partial だけをバイトコードにコンパイルし、
 *          それ以外の {{#partialdef}} は生成されるバイトコードから捨てる。
 *          前方参照不可の制約から、依存先を先にコンパイルするトポロジカル順で構成される。
 *
 * @tparam Tmpl       コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam PartialName レンダリングする partial の名前（fixed_string リテラル）
 * @tparam TrimBlocks   先頭/末尾ホワイトスペーストリム（0=無効）
 * @tparam LstripBlocks 左ストリップブロック（0=無効）
 * @tparam T          コンテキスト値の型（glz::meta<T> 要特殊化）
 * @param value       コンテキスト値の const 参照
 * @return expected<std::string> レンダリング結果、またはエラー
 */
template <fixed_string Tmpl, fixed_string PartialName, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T>
[[nodiscard]] expected<std::string> render_partial(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  constexpr auto target_sv = detail::nttp_string_view(PartialName);
  constexpr auto closure = detail::compute_partial_closure(D::parsed, D::tmpl_sv, target_sv);
  static_assert(closure.found, "injamm: {{#partialdef <PartialName>}} not found in the template.");
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  auto& bc = detail::nttp_selected_partial_holder<D, PartialName, T>();
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  // ponytail: 対象 partial は post-order DFS で末尾に push されるため必ず back()
  return detail::bc_execute(*bc.partial_entries.back().bc, value);
}

template <fixed_string Tmpl, fixed_string PartialName, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T, typename Sink>
  requires (detail::output_sink<Sink> && !std::same_as<std::remove_cvref_t<Sink>, std::string>)
[[nodiscard]] expected<void> render_partial(T const& value, Sink& sink) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  constexpr auto target_sv = detail::nttp_string_view(PartialName);
  constexpr auto closure = detail::compute_partial_closure(D::parsed, D::tmpl_sv, target_sv);
  static_assert(closure.found, "injamm: {{#partialdef <PartialName>}} not found in the template.");
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  auto& bc = detail::nttp_selected_partial_holder<D, PartialName, T>();
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  // ponytail: 対象 partial は post-order DFS で末尾に push されるため必ず back()
  return detail::bc_execute_into_sink(*bc.partial_entries.back().bc, value, sink);
}

#if INJAMM_HAS_FROZENCHARS

/**
 * @brief NTTP ベースの名前付き partial レンダリング（FrozenString テンプレート対応）
 *
 * @details render の auto Tmpl オーバーロードと同様、frozenchars::FrozenString
 *          (_fs リテラル) をテンプレート文字列として受け取れる。fixed_string 版と
 *           overload セットを分けるため、Tmpl が fixed_string でない場合のみ選択される。
 */
template <auto Tmpl, bool TrimBlocks = false, bool LstripBlocks = false, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)>)
[[nodiscard]] expected<std::string> render_partial(T const& value, std::string_view partial_name) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  auto& bc = detail::nttp_partial_bytecode_holder<D, T>();
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  auto it = std::find_if(bc.partial_entries.begin(), bc.partial_entries.end(), [&](auto const& e) { return !e.local && e.name == partial_name; });
  if (it == bc.partial_entries.end())
    return std::unexpected(error_ctx{0, error_code::unknown_key, partial_name});
  return detail::bc_execute(*it->bc, value);
}

template <auto Tmpl, bool TrimBlocks = false, bool LstripBlocks = false, typename T, typename Sink>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)> && detail::output_sink<Sink>)
[[nodiscard]] expected<void> render_partial(T const& value, std::string_view partial_name, Sink& sink) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  auto& bc = detail::nttp_partial_bytecode_holder<D, T>();
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  auto it = std::find_if(bc.partial_entries.begin(), bc.partial_entries.end(), [&](auto const& e) { return !e.local && e.name == partial_name; });
  if (it == bc.partial_entries.end())
    return std::unexpected(error_ctx{0, error_code::unknown_key, partial_name});
  return detail::bc_execute_into_sink(*it->bc, value, sink);
}

template <auto Tmpl, typename Reg, bool TrimBlocks = false, bool LstripBlocks = false, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)> && detail::is_ct_partials_v<Reg>)
[[nodiscard]] expected<std::string> render_partial(T const& value, std::string_view partial_name) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T, Reg>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  auto& bc = detail::nttp_partial_bytecode_holder<D, T>();
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  auto it = std::find_if(bc.partial_entries.begin(), bc.partial_entries.end(), [&](auto const& e) { return !e.local && e.name == partial_name; });
  if (it == bc.partial_entries.end())
    return std::unexpected(error_ctx{0, error_code::unknown_key, partial_name});
  return detail::bc_execute(*it->bc, value);
}

/**
 * @brief NTTP ベースの名前付き partial レンダリング（FrozenString テンプレート + 文字列リテラル名）
 *
 * @details Tmpl に FrozenString (_fs リテラル) を、PartialName に文字列リテラル
 *          ("name") を指定する組み合わせ用。partial 名は fixed_string に consteval
 *          構築される。
 */
template <auto Tmpl, fixed_string PartialName, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)>)
[[nodiscard]] expected<std::string> render_partial(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  constexpr auto target_sv = detail::nttp_string_view(PartialName);
  constexpr auto closure = detail::compute_partial_closure(D::parsed, D::tmpl_sv, target_sv);
  static_assert(closure.found, "injamm: {{#partialdef <PartialName>}} not found in the template.");
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  // 直線 only の partial 本文はコンパイル時アンロールで高速実行（htmx の行更新など）
  constexpr auto body_start = closure.count > 0 ? D::parsed.partial_body_starts[closure.order[closure.count - 1]] : closure.nested_body_start;
  constexpr auto body_end   = closure.count > 0 ? D::parsed.partial_body_ends[closure.order[closure.count - 1]] : closure.nested_body_end;
  using BodyD = detail::nttp_partial_body_data<Tmpl, body_start, body_end, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (detail::ct_is_unrollable(BodyD::ct_bc))
    return detail::ct_executor<BodyD, T>::run(value);
  else {
    auto& bc = detail::nttp_selected_partial_holder<D, PartialName, T>();
    if (bc.error.ec != error_code::none)
      return std::unexpected(bc.error);
    // ponytail: 対象 partial は post-order DFS で末尾に push されるため必ず back()
    return detail::bc_execute(*bc.partial_entries.back().bc, value);
  }
}

template <auto Tmpl, fixed_string PartialName, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T, typename Sink>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)> && detail::output_sink<Sink> &&
            !std::same_as<std::remove_cvref_t<Sink>, std::string>)
[[nodiscard]] expected<void> render_partial(T const& value, Sink& sink) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  constexpr auto target_sv = detail::nttp_string_view(PartialName);
  constexpr auto closure = detail::compute_partial_closure(D::parsed, D::tmpl_sv, target_sv);
  static_assert(closure.found, "injamm: {{#partialdef <PartialName>}} not found in the template.");
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  auto& bc = detail::nttp_selected_partial_holder<D, PartialName, T>();
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  // ponytail: 対象 partial は post-order DFS で末尾に push されるため必ず back()
  return detail::bc_execute_into_sink(*bc.partial_entries.back().bc, value, sink);
}

/**
 * @brief NTTP ベースの名前付き partial レンダリング（partial 名をテンプレート引数で指定、FrozenString 対応）
 */
template <auto Tmpl, auto PartialName, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)> && !detail::is_fixed_string_type_v<decltype(PartialName)>)
[[nodiscard]] expected<std::string> render_partial(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  constexpr auto target_sv = detail::nttp_string_view(PartialName);
  constexpr auto closure = detail::compute_partial_closure(D::parsed, D::tmpl_sv, target_sv);
  static_assert(closure.found, "injamm: {{#partialdef <PartialName>}} not found in the template.");
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  // 直線 only の partial 本文はコンパイル時アンロールで高速実行（htmx の行更新など）
  constexpr auto body_start = closure.count > 0 ? D::parsed.partial_body_starts[closure.order[closure.count - 1]] : closure.nested_body_start;
  constexpr auto body_end   = closure.count > 0 ? D::parsed.partial_body_ends[closure.order[closure.count - 1]] : closure.nested_body_end;
  using BodyD = detail::nttp_partial_body_data<Tmpl, body_start, body_end, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (detail::ct_is_unrollable(BodyD::ct_bc))
    return detail::ct_executor<BodyD, T>::run(value);
  else {
    auto& bc = detail::nttp_selected_partial_holder<D, PartialName, T>();
    if (bc.error.ec != error_code::none)
      return std::unexpected(bc.error);
    // ponytail: 対象 partial は post-order DFS で末尾に push されるため必ず back()
    return detail::bc_execute(*bc.partial_entries.back().bc, value);
  }
}

template <auto Tmpl, auto PartialName, std::same_as<bool> auto TrimBlocks = false, std::same_as<bool> auto LstripBlocks = false, typename T, typename Sink>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)> && !detail::is_fixed_string_type_v<decltype(PartialName)> &&
            detail::output_sink<Sink> && !std::same_as<std::remove_cvref_t<Sink>, std::string>)
[[nodiscard]] expected<void> render_partial(T const& value, Sink& sink) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  constexpr auto target_sv = detail::nttp_string_view(PartialName);
  constexpr auto closure = detail::compute_partial_closure(D::parsed, D::tmpl_sv, target_sv);
  static_assert(closure.found, "injamm: {{#partialdef <PartialName>}} not found in the template.");
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  auto& bc = detail::nttp_selected_partial_holder<D, PartialName, T>();
  if (bc.error.ec != error_code::none)
    return std::unexpected(bc.error);
  // ponytail: 対象 partial は post-order DFS で末尾に push されるため必ず back()
  return detail::bc_execute_into_sink(*bc.partial_entries.back().bc, value, sink);
}

#endif

}  // namespace injamm
