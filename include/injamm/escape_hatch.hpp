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
#include "bytecode_debug.hpp"
#include "bytecode_compile.hpp"
#include "bytecode_exec.hpp"
#include <memory>
#include <array>
#include <tuple>

#if __has_include(<frozenchars.hpp>)
#include <frozenchars.hpp>
#define INJAMM_HAS_FROZENCHARS 1
#endif

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

#if __has_include(<frozenchars.hpp>)
  /**
   * @brief FrozenString から構築する
   *
   * @param fs frozenchars のコンパイル時文字列
   */
  constexpr fixed_string(frozenchars::FrozenString<N> const& fs) noexcept {
    for (std::size_t i = 0; i < fs.size(); ++i) {
      data[i] = fs.data()[i];
    }
    data[fs.size()] = '\0';
  }
#endif

  /**
   * @brief 文字列長を返す（ヌル終端除く）
   *
   * @return std::size_t 文字数（N - 1）
   */
  [[nodiscard]] constexpr std::size_t size() const noexcept {
    std::size_t len = 0;
    while (len < N && data[len] != '\0') ++len;
    return len;
  }
};

#if __has_include(<frozenchars.hpp>)
template <std::size_t M>
fixed_string(frozenchars::FrozenString<M>) -> fixed_string<M>;
#endif

namespace detail {

template <typename T>
inline constexpr bool always_false = false;

/**
 * @brief NTTP 文字列から string_view を取得する
 *
 * @details fixed_string (data メンバ) と FrozenString (data() メソッド) の
 *          両方に対応する。auto NTTP で統一するためのブリッジ関数。
 *
 * @tparam S NTTP 文字列型
 * @param s  NTTP 文字列インスタンス
 * @return std::string_view 内部バッファを指すビュー
 */
template <typename S>
constexpr std::string_view nttp_string_view(S const& s) noexcept {
  if constexpr (requires { s.data; }) {
    return {s.data, s.size()};
  } else if constexpr (requires { s.data(); }) {
    return {s.data(), s.size()};
  } else {
    static_assert(always_false<S>, 
      "injamm: Unsupported NTTP string type. "
      "Expected injamm::fixed_string or frozenchars::FrozenString. "
      "Ensure your type has either a '.data' member or a '.data()' method.");
  }
}

/**
 * @brief コンパイル時パース実装（SoA 形式）
 *
 * @details NTTP テンプレート引数から文字列を取り出し、
 *          ct_parse_into で SoA 形式のチャンク配列にパースする。
 *          パース結果は constexpr で確定し、実行時オーバーヘッドはゼロ。
 *          fixed_string / FrozenString の両方に対応。
 *
 * @tparam Tmpl テンプレート文字列（NTTP）
 * @return ct_parsed_template<Tmpl.size() + 1> パース済みチャンク配列
 */
template <auto Tmpl, bool TrimBlocks = false, bool LstripBlocks = false>
consteval auto parse_fixed_impl() -> ct_parsed_template<Tmpl.size() + 1> {
  ct_parse_context<Tmpl.size() + 1> ctx;
  ct_parse_into(ctx, nttp_string_view(Tmpl), TrimBlocks, LstripBlocks);
  return ctx.tmpl;
}

/**
 * @brief コンパイル時キー・バリュー参照テーブル
 *
 * @details NTTP 文字列のペアをキー・バリューとして格納し、文字列キーから
 *          定数値を O(n) 検索する。偶数の entries が必要（キー・バリューのペア）。
 *          見つからない場合は空の string_view を返す。
 *          fixed_string / FrozenString の両方に対応。
 *
 * @tparam Entries NTTP 文字列のパラメータパック（キー1, 値1, キー2, 値2, ...）
 */
template <auto... Entries>
struct ct_var_table {
  static constexpr std::size_t num = sizeof...(Entries);
  static_assert(num % 2 == 0, 
    "injamm: @var entries must be key-value pairs (even count). "
    "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data)");

  static constexpr std::array<std::string_view, num> entries{
    nttp_string_view(Entries)...
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
 *          fixed_string / FrozenString の両方に対応。
 *
 * @tparam Tmpl    元テンプレート文字列（NTTP）
 * @tparam Entries キー・バリューペアのパラメータパック（NTTP）
 */
template <auto Tmpl, auto... Entries>
struct ct_expanded_template {
  using table = ct_var_table<Entries...>;

  static constexpr std::size_t compute_size() {
    auto sv = nttp_string_view(Tmpl);
    std::size_t sz = 0;
    std::size_t pos = 0;
    while (pos < sv.size()) {
      auto var_start = constexpr_find(sv, "@var(", pos);
      auto partial_start = constexpr_find(sv, "{{>", pos);
      auto next = [&] {
        if (var_start == std::string_view::npos) return partial_start;
        if (partial_start == std::string_view::npos) return var_start;
        return std::min(var_start, partial_start);
      }();
      if (next == std::string_view::npos) {
        sz += sv.size() - pos;
        break;
      }
      sz += next - pos;

      if (next == var_start) {
        auto close = constexpr_find(sv, ')', var_start + 5);
        if (close == std::string_view::npos) {
          sz += sv.size() - var_start;
          break;
        }
        auto name = sv.substr(var_start + 5, close - var_start - 5);
        auto val = table::lookup(name);
        sz += val.empty() ? (close - var_start + 1) : val.size();
        pos = close + 1;
      } else {
        auto close = constexpr_find(sv, "}}", partial_start + 3);
        if (close == std::string_view::npos) {
          sz += sv.size() - partial_start;
          break;
        }
        auto name = trim_sv(sv.substr(partial_start + 3, close - partial_start - 3));
        auto val = table::lookup(name);
        sz += val.empty() ? (close - partial_start + 2) : val.size();
        pos = close + 2;
      }
    }
    return sz;
  }

  static constexpr std::size_t expanded_size = compute_size();

  static constexpr std::array<char, expanded_size + 1> data = []() {
    std::array<char, expanded_size + 1> arr{};
    auto sv = nttp_string_view(Tmpl);
    std::size_t out = 0;
    std::size_t pos = 0;
    while (pos < sv.size()) {
      auto var_start = constexpr_find(sv, "@var(", pos);
      auto partial_start = constexpr_find(sv, "{{>", pos);
      auto next = [&] {
        if (var_start == std::string_view::npos) return partial_start;
        if (partial_start == std::string_view::npos) return var_start;
        return std::min(var_start, partial_start);
      }();
      if (next == std::string_view::npos) {
        while (pos < sv.size())
          arr[out++] = sv[pos++];
        break;
      }
      while (pos < next)
        arr[out++] = sv[pos++];

      if (next == var_start) {
        auto close = constexpr_find(sv, ')', var_start + 5);
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
      } else {
        auto close = constexpr_find(sv, "}}", partial_start + 3);
        if (close == std::string_view::npos) {
          while (pos < sv.size())
            arr[out++] = sv[pos++];
          break;
        }
        auto name = trim_sv(sv.substr(partial_start + 3, close - partial_start - 3));
        auto val = table::lookup(name);
        if (!val.empty()) {
          for (auto c : val)
            arr[out++] = c;
        } else {
          for (auto i = partial_start; i < close + 2; ++i)
            arr[out++] = sv[i];
        }
        pos = close + 2;
      }
    }
    return arr;
  }();
};

// ---- constexpr 計算を保持する thin-wrapper 用構造体 ----

template <auto Tmpl, bool Trim, bool Lstrip, typename T>
struct nttp_render_data {
  static constexpr auto parsed   = detail::parse_fixed_impl<Tmpl, Trim, Lstrip>();
  static constexpr auto resolved = detail::resolve_field_indices<T>(parsed);
  static constexpr auto ct_bc    = detail::ct_chunks_to_bytecode<T>(resolved);
};

template <auto Tmpl, typename T, auto... Entries>
struct nttp_atvar_data {
  using ET = detail::ct_expanded_template<Tmpl, Entries...>;
  static constexpr auto parsed = []() {
    detail::ct_parse_context<ET::expanded_size + 1> ctx;
    detail::ct_parse_into(ctx, std::string_view{ET::data.data(), ET::expanded_size});
    return detail::resolve_field_indices<T>(ctx.tmpl);
  }();
  static constexpr auto ct_bc = detail::ct_chunks_to_bytecode<T>(parsed);
};

template <typename Data>
detail::bytecode const& nttp_bytecode_holder() {
  static detail::bytecode const bc = detail::to_bytecode(Data::ct_bc);
  return bc;
}

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
template <fixed_string Tmpl, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
[[nodiscard]] expected<std::string> render(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute(detail::nttp_bytecode_holder<D>(), value);
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
template <fixed_string Tmpl, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
[[nodiscard]] expected<void> render(T const& value, std::string& out) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into(detail::nttp_bytecode_holder<D>(), value, out);
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
  requires(sizeof...(Entries) > 0)
[[nodiscard]] expected<std::string> render(T const& value) {
  static_assert(sizeof...(Entries) % 2 == 0,
    "injamm: @var entries must be key-value pairs (even count). "
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
  requires(sizeof...(Entries) > 0)
[[nodiscard]] expected<void> render(T const& value, std::string& out) {
  static_assert(sizeof...(Entries) % 2 == 0,
    "injamm: @var entries must be key-value pairs (even count). "
    "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data, out)");
  using D = detail::nttp_atvar_data<Tmpl, T, Entries...>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into(detail::nttp_bytecode_holder<D>(), value, out);
}

/**
 * @brief コンテナを NTTP 名でバインドした bound_context を生成する
 *
 * @details 複数のコンテナを NTTP 文字列名と対応付けた bound_context を返す。
 *          戻り値の bound_context は参照を保持するため、元コンテナの生存期間内で使用すること。
 *          渡すコンテナ数と Names の数が一致しない場合はコンパイルエラーとなる。
 *
 * @tparam Names      バインドする変数名の NTTP fixed_string パック
 * @tparam Containers コンテナ型パック（推論）
 * @param  values     バインドするコンテナへの const 参照パック
 * @return detail::bound_context<detail::name_list<Names...>, Containers...>
 */
template <fixed_string... Names, typename... Containers>
[[nodiscard]] auto bind(Containers const&... values)
  -> detail::bound_context<detail::name_list<Names...>, Containers...>
{
  static_assert(sizeof...(Names) == sizeof...(Containers),
                "injamm: bind() requires the same number of names and containers. "
                "Example: bind<\"items\", \"user\">(items, user)");
  return detail::bound_context<detail::name_list<Names...>, Containers...>{std::forward_as_tuple(values...)};
}

template <typename T>
[[nodiscard]] auto bind(T const& value)
  -> detail::bound_context<detail::name_list<fixed_string{"_"}>, T>
{
  return detail::bound_context<detail::name_list<fixed_string{"_"}>, T>{std::forward_as_tuple(value)};
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
  engine() = delete;

  /**
   * @brief テンプレート文字列から構築（実行時コンパイル）
   *
   * @param tmpl テンプレート文字列（std::string_view）
   * @param trim_blocks 閉じタグ後の改行を除去する（デフォルト false）
   * @param lstrip_blocks ブロックタグ前の空白を除去する（デフォルト false）
   */
  explicit engine(std::string_view tmpl, bool trim_blocks = false, bool lstrip_blocks = false)
    : bc_(detail::bc_compile<T>(tmpl, trim_blocks, lstrip_blocks)) {}

  template <class ConstMap>
  explicit engine(std::string_view tmpl, ConstMap const& consts, bool trim_blocks = false, bool lstrip_blocks = false)
    : bc_(detail::bc_compile<T>(tmpl, consts, trim_blocks, lstrip_blocks)) {}

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
   * @brief 名前付き partial のみをレンダリングする
   *
   * @param value コンテキスト値の const 参照
   * @param partial_name レンダリングする partial の名前
   * @return expected<std::string> レンダリング結果、またはエラー
   */
  [[nodiscard]] expected<std::string> render(T const& value, std::string_view partial_name) const {
    if (bc_.error.ec != error_code::none) {
      return std::unexpected(bc_.error);
    }
    auto it = std::find_if(bc_.partial_entries.begin(), bc_.partial_entries.end(),
                            [&](auto const& e) { return e.name == partial_name; });
    if (it == bc_.partial_entries.end()) {
      return std::unexpected(error_ctx{0, error_code::unknown_key, partial_name});
    }
    return detail::bc_execute(*it->bc, value);
  }

  /**
   * @brief レンダリング結果を既存バッファに書き込む（バッファ再利用用）
   *
   * @param value コンテキスト値の const 参照
   * @param out 出力先バッファ（内容はクリアされる）
   * @return expected<void> 実行結果、またはエラー
   */
  [[nodiscard]] expected<void> render(T const& value, std::string& out) const {
    if (bc_.error.ec != error_code::none) {
      return std::unexpected(bc_.error);
    }
    return detail::bc_execute_into(bc_, value, out);
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
