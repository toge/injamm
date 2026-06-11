#pragma once

/**
 * @file escape_hatch.hpp
 * @brief NTTP コンパイル時パーサーとバイトコード VM の公開 API
 *
 * @details fixed_string（NTTP 文字列）とコンパイル時パースによる
 *          true compile-time template rendering（render 関数）、
 *          および実行時コンパイル + Bytecode VM によるランタイムレンダリング
 *          （engine クラス）の 2 系統の API を提供する。
 *          SoA（Struct of Arrays）形式を採用しキャッシュ効率を向上している。
 */

#include "types.hpp"
#include "ct_chunk.hpp"
#include "ct_parse.hpp"
#include "bytecode_ct_compile.hpp"
#include "bytecode.hpp"
#include "bytecode_compile.hpp"
#include "bytecode_exec.hpp"
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

/**
 * @brief コンパイル時キー・バリュー参照テーブル
 *
 * @details fixed_string NTTP のペアをキー・バリューとして格納し、文字列キーから
 *          定数値を O(n) 検索する。偶数の entries が必要（キー・バリューのペア）。
 *          見つからない場合は空の string_view を返す。
 *
 * @tparam Entries fixed_string のパラメータパック（キー1, 値1, キー2, 値2, ...）
 */
template <fixed_string... Entries>
struct ct_var_table {
  static constexpr std::size_t num = sizeof...(Entries);
  static_assert(num % 2 == 0, "@var entries must be key-value pairs (even count)");

  static constexpr std::array<std::string_view, num> entries{
    std::string_view{Entries.data, Entries.size()}...
  };

  static constexpr std::string_view lookup(std::string_view key) noexcept {
    for (std::size_t i = 0; i < num; i += 2) {
      if (entries[i] == key)
        return entries[i + 1];
    }
    return {};
  }
};

/**
 * @brief コンパイル時 @var(name) 展開テンプレート
 *
 * @details テンプレート文字列中の @var(name) を ct_var_table の定数値に
 *          コンパイル時に展開する。サイズ計算と実データの2段階で動作する。
 *
 * @tparam Tmpl    元テンプレート文字列（fixed_string）
 * @tparam Entries キー・バリューペアのパラメータパック
 */
template <fixed_string Tmpl, fixed_string... Entries>
struct ct_expanded_template {
  using table = ct_var_table<Entries...>;

  static constexpr std::size_t compute_size() {
    auto sv = std::string_view{Tmpl.data, Tmpl.size()};
    std::size_t sz = 0;
    std::size_t pos = 0;
    while (pos < sv.size()) {
      auto var_start = sv.find("@var(", pos);
      if (var_start == std::string_view::npos) {
        sz += sv.size() - pos;
        break;
      }
      sz += var_start - pos;
      auto close = sv.find(")", var_start + 5);
      if (close == std::string_view::npos) {
        sz += sv.size() - var_start;
        break;
      }
      auto name = sv.substr(var_start + 5, close - var_start - 5);
      auto val = table::lookup(name);
      sz += val.empty() ? (close - var_start + 1) : val.size();
      pos = close + 1;
    }
    return sz;
  }

  static constexpr std::size_t expanded_size = compute_size();

  static constexpr std::array<char, expanded_size + 1> data = []() {
    std::array<char, expanded_size + 1> arr{};
    auto sv = std::string_view{Tmpl.data, Tmpl.size()};
    std::size_t out = 0;
    std::size_t pos = 0;
    while (pos < sv.size()) {
      auto var_start = sv.find("@var(", pos);
      if (var_start == std::string_view::npos) {
        while (pos < sv.size())
          arr[out++] = sv[pos++];
        break;
      }
      while (pos < var_start)
        arr[out++] = sv[pos++];
      auto close = sv.find(")", var_start + 5);
      if (close == std::string_view::npos) {
        while (pos < sv.size())
          arr[out++] = sv[pos++];
        break;
      }
      auto name = sv.substr(var_start + 5, close - var_start - 5);
      auto val = table::lookup(name);
      if (!val.empty()) {
        for (auto c : val)
          arr[out++] = c;
      } else {
        for (auto i = var_start; i <= close; ++i)
          arr[out++] = sv[i];
      }
      pos = close + 1;
    }
    return arr;
  }();
};

} // namespace detail

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
template <fixed_string Tmpl, typename T>
[[nodiscard]] expected<std::string> render(T const& value) {
  constexpr auto parsed = detail::parse_fixed_impl<Tmpl>();
  constexpr auto resolved = detail::resolve_field_indices<T>(parsed);
  constexpr auto ct_bc = detail::ct_chunks_to_bytecode<T>(resolved);
  if (ct_bc.error.ec != error_code::none)
    return std::unexpected(ct_bc.error);
  auto bc = detail::to_bytecode(ct_bc);
  return detail::bc_execute(bc, value);
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
  requires(sizeof...(Entries) > 0 && sizeof...(Entries) % 2 == 0)
[[nodiscard]] expected<std::string> render(T const& value) {
  using ET = detail::ct_expanded_template<Tmpl, Entries...>;
  constexpr std::string_view expanded_sv{ET::data.data(), ET::expanded_size};
  constexpr auto parsed = [&]() {
    detail::ct_parse_context<ET::expanded_size + 1> ctx;
    detail::ct_parse_into(ctx, expanded_sv);
    return detail::resolve_field_indices<T>(ctx.tmpl);
  }();
  constexpr auto ct_bc = detail::ct_chunks_to_bytecode<T>(parsed);
  if (ct_bc.error.ec != error_code::none)
    return std::unexpected(ct_bc.error);
  auto bc = detail::to_bytecode(ct_bc);
  return detail::bc_execute(bc, value);
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
class engine {
  detail::bytecode bc_;

public:
  engine() = default;

  /**
   * @brief テンプレート文字列から構築（実行時コンパイル）
   *
   * @param tmpl テンプレート文字列（std::string_view）
   */
  explicit engine(std::string_view tmpl) : bc_(detail::bc_compile<T>(tmpl)) {}

  template <class ConstMap>
  explicit engine(std::string_view tmpl, ConstMap const& consts) : bc_(detail::bc_compile<T>(tmpl, consts)) {}

  /**
   * @brief レンダリングを実行する
   *
   * @param value コンテキスト値の const 参照
   * @return expected<std::string> レンダリング結果、またはエラー
   */
  [[nodiscard]] expected<std::string> render(T const& value) const {
    if (bc_.error.ec != error_code::none) {
      return std::unexpected(bc_.error);
    }
    return detail::bc_execute(bc_, value);
  }

  /**
   * @brief コンパイル済みバイトコードを可読な形式に逆アセンブルする
   *
   * @details デバッグと最適化のために、内部バイトコードを人間に読みやすい形式で
   *          出力する。命令列・リテラルテーブル・変数参照テーブルを含む。
   * @return std::string 逆アセンブル結果
   */
  [[nodiscard]] std::string disassemble() const {
    return bc_.disassemble();
  }
};

} // namespace injamm
