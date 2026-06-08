#pragma once

/**
 * @file escape_hatch.hpp
 * @brief NTTP コンパイル時パーサーとバイトコード VM の公開 API
 *
 * @details fixed_string（NTTP 文字列）とコンパイル時パースによる
 *          true compile-time template rendering（render 関数）、
 *          および実行時コンパイル + Bytecode VM によるランタイムレンダリング
 *          （bc_template クラス）の 2 系統の API を提供する。
 *          SoA（Struct of Arrays）形式を採用しキャッシュ効率を向上している。
 */

#include "injamm.hpp"
#include "detail/ct_chunk.hpp"
#include "detail/ct_parse.hpp"
#include "detail/ct_render.hpp"
#include "detail/bytecode.hpp"
#include "detail/bytecode_compile.hpp"
#include "detail/bytecode_exec.hpp"
#include <array>

namespace injamm {

/**
 * @brief コンパイル時文字列定数（NTTP 用）
 *
 * @details C++20 NTTP（Non-Type Template Parameter）として文字列リテラルを
 *          受け渡すためのラッパ型。consteval コンストラクタで char 配列を
 *          メンバにコピー保持する。
 *
 * @tparam N 文字配列の長さ（ヌル終端を含む）
 */
template <std::size_t N>
struct fixed_string {
  char data[N]{}; /**< @brief 内部バッファ（ヌル終端文字列） */

  fixed_string() = default;

  /**
   * @brief 文字列リテラルから構築する
   *
   * @param str ヌル終端された文字配列リテラル
   */
  consteval fixed_string(char const (&str)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
      data[i] = str[i];
    }
  }

  /**
   * @brief 文字列長を返す（ヌル終端除く）
   *
   * @return std::size_t 文字数（N - 1）
   */
  [[nodiscard]] consteval std::size_t size() const noexcept { return N - 1; }
};

namespace detail {

/**
 * @brief コンパイル時パース実装（SoA 形式）
 *
 * @details fixed_string テンプレート引数から文字列を取り出し、
 *          ct_parse_into で SoA 形式のチャンク配列にパースする。
 *          パース結果は constexpr で確定し、実行時オーバーヘッドはゼロ。
 *
 * @tparam Tmpl テンプレート文字列（fixed_string）
 * @return ct_parsed_template<Tmpl.size() + 1> パース済みチャンク配列
 */
template <fixed_string Tmpl>
consteval auto parse_fixed_impl() -> ct_parsed_template<Tmpl.size() + 1> {
  ct_parse_context<Tmpl.size() + 1> ctx;
  std::string_view sv{Tmpl.data, Tmpl.size()};
  ct_parse_into(ctx, sv);
  return ctx.tmpl;
}

} // namespace detail

/**
 * @brief NTTP ベースのレンダリング（真のコンパイル時パース、SoA 版）
 *
 * @details テンプレート引数 Tmpl で渡された文字列をコンパイル時にパースし、
 *          実行時には変数値の埋め込みのみを行う。
 *          {{var}} は HTML エスケープ付き、{{{var}}} は生出力。
 *          セクション / if は非対応（bc_template を使用すること）。
 *
 * @tparam Tmpl コンパイル時テンプレート文字列（fixed_string リテラル）
 * @tparam T    コンテキスト値の型（glz::meta<T> 要特殊化）
 * @param value コンテキスト値の const 参照
 * @return expected<std::string> レンダリング結果、またはエラー（error_ctx）
 */
template <fixed_string Tmpl, class T>
[[nodiscard]] inline expected<std::string> render(T const& value) {
  constexpr auto fp = detail::parse_fixed_impl<Tmpl>();
  std::string out;
  out.reserve(Tmpl.size() * 2);
  auto r = detail::ct_render_chunks<stencil_tag>(out, fp, 0, fp.size, value, value, nullptr);
  if (!r) {
    return std::unexpected(r.error());
  }
  return out;
}

/**
 * @brief バイトコード VM（実行時コンパイル）
 *
 * @details 実行時にテンプレート文字列をパースし、中間表現（Bytecode）に
 *          コンパイルしてからレンダリングを行う。
 *          ct_render に比べて柔軟性が高く、セクション / if / @変数 / ネストパス
 *          などの全機能をサポートする。
 *
 * @tparam T コンテキスト値の型（glz::meta<T> 要特殊化）
 */
template <class T>
class bc_template {
  detail::bytecode bc_;

public:
  bc_template() = default;

  /**
   * @brief テンプレート文字列から構築（実行時コンパイル）
   *
   * @param tmpl テンプレート文字列（std::string_view）
   */
  explicit bc_template(std::string_view tmpl) : bc_(detail::bc_compile<T>(tmpl)) {}

  /**
   * @brief レンダリングを実行する
   *
   * @param value コンテキスト値の const 参照
   * @return expected<std::string> レンダリング結果、またはエラー
   */
  [[nodiscard]] expected<std::string> render(T const& value) const {
    return detail::bc_execute(bc_, value);
  }
};

} // namespace injamm
