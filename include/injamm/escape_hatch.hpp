#pragma once

/**
 * @file escape_hatch.hpp
 * @brief NTTP コンピイル時パーサーとバイトコード VM の公開 API
 *
 * @details fixed_string（NTTP 文字列）とコンピイル時パースによる
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
#include <array>
#include <memory>
#include <tuple>

#if __has_include(<frozenchars/mod/core.hpp>)
#include <frozenchars/mod/core.hpp>
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

namespace detail {

  template <typename T>
  inline constexpr bool always_false = false;

  template <typename T>
  struct is_fixed_string_type : std::false_type {};
  template <std::size_t N>
  struct is_fixed_string_type<fixed_string<N>> : std::true_type {};
  template <typename T>
  constexpr bool is_fixed_string_type_v = is_fixed_string_type<std::remove_cvref_t<T>>::value;

  /**
   * @brief CT 用 partial レジストリ（型パック）
   *
   * @details 名前・本文の fixed_string ペアを可変長で保持する。
   *          サイズ指定は不要。この型を render / render_partial のテンプレート
   *          引数（第2引数）として渡すことで、複数の render 呼び出し間で
   *          partial レジストリを共有できる。
   *
   * @tparam Pairs "name1","body1","name2","body2",... の順に並ぶ固定文字列パック
   */
  template <fixed_string... Pairs>
  struct ct_partials {
    static constexpr std::size_t count = sizeof...(Pairs) / 2;
    static_assert(sizeof...(Pairs) % 2 == 0, "injamm: ct_partials requires name/body pairs (even count). "
                                            "Example: ct_partials<\"name\", \"body\", \"name2\", \"body2\">");
  };

  template <typename T>
  struct is_ct_partials : std::false_type {};
  template <fixed_string... Pairs>
  struct is_ct_partials<ct_partials<Pairs...>> : std::true_type {};
  template <typename T>
  constexpr bool is_ct_partials_v = is_ct_partials<std::remove_cvref_t<T>>::value;

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
    if constexpr (requires { s.data(); }) {
      return {s.data(), s.size()};
    } else if constexpr (requires { s.data; }) {
      return {s.data, s.size()};
    } else {
      static_assert(always_false<S>, "injamm: Unsupported NTTP string type. "
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
    auto                              sv = nttp_string_view(Tmpl);
    ct_parse_context<Tmpl.size() + 1> ctx;

    // 事前スキャン: {{#partialdef name}}...{{/partialdef}} を抽出
    {
      std::size_t pos = 0;
      while (pos < sv.size()) {
        auto pdef_start = constexpr_find(sv, "{{#partialdef", pos);
        if (pdef_start == std::string_view::npos)
          break;
        auto tag_end = constexpr_find(sv, "}}", pdef_start);
        if (tag_end == std::string_view::npos)
          break;
        auto inner = trim_sv(sv.substr(pdef_start + 2, tag_end - pdef_start - 2));
        if (!inner.starts_with("#partialdef ")) {
          pos = tag_end + 2;
          continue;
        }
        auto name      = trim_sv(inner.substr(12));
        auto close_tag = constexpr_find(sv, "{{/partialdef}}", tag_end + 2);
        if (close_tag == std::string_view::npos)
          break;
        // {{#partialdef name [now] [local]}} の修飾子を検出（順不同、併用可）
        //   now   : 即時展開し、後で {{#partial name}} で再利用可能
        //   local : 即時展開のみ。名前検索では参照不可（外部から使えない）
        // （実際の即時展開は ct_parse_into 内で key から判定して合成する）
        bool is_local = false;
        {
          auto sp = constexpr_find(name, ' ');
          if (sp != std::string_view::npos) {
            auto base = trim_sv(name.substr(0, sp));
            auto rest = name.substr(sp + 1);
            while (!rest.empty()) {
              auto nsp = constexpr_find(rest, ' ');
              auto tok = trim_sv(rest.substr(0, nsp));
              if (tok == "local")
                is_local = true;
              rest = (nsp == std::string_view::npos) ? std::string_view{} : rest.substr(nsp + 1);
            }
            name = base;
          }
        }
        auto& tmpl                                   = ctx.tmpl;
        tmpl.partial_names[tmpl.partial_count]       = name;
        tmpl.partial_local[tmpl.partial_count]       = is_local;
        tmpl.partial_body_starts[tmpl.partial_count] = tag_end + 2;
        tmpl.partial_body_ends[tmpl.partial_count]   = close_tag;
        ++tmpl.partial_count;
        tmpl.partial_total = tmpl.partial_count;
        pos = close_tag + 15;
      }
    }

    ct_parse_into(ctx, sv, TrimBlocks, LstripBlocks);
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
    static_assert(num % 2 == 0, "injamm: @var entries must be key-value pairs (even count). "
                                "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data)");

    static constexpr std::array<std::string_view, num> entries{nttp_string_view(Entries)...};

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
  consteval std::size_t ct_compute_expanded_size() {
    auto        sv  = nttp_string_view(Tmpl);
    std::size_t sz  = 0;
    std::size_t pos = 0;
    using table_t = ct_var_table<Entries...>;
    while (pos < sv.size()) {
      auto var_start     = constexpr_find(sv, "@var(", pos);
      auto partial_start = constexpr_find(sv, "{{>", pos);
      auto next          = (var_start == std::string_view::npos)        ? partial_start
                         : (partial_start == std::string_view::npos) ? var_start
                                                                   : std::min(var_start, partial_start);
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
        auto val  = table_t::lookup(name);
        sz += val.empty() ? (close - var_start + 1) : val.size();
        pos = close + 1;
      } else {
        auto close = constexpr_find(sv, "}}", partial_start + 3);
        if (close == std::string_view::npos) {
          sz += sv.size() - partial_start;
          break;
        }
        auto name = trim_sv(sv.substr(partial_start + 3, close - partial_start - 3));
        auto val  = table_t::lookup(name);
        sz += val.empty() ? (close - partial_start + 2) : val.size();
        pos = close + 2;
      }
    }
    return sz;
  }

  template <auto Tmpl, auto... Entries>
  consteval std::array<char, ct_compute_expanded_size<Tmpl, Entries...>() + 1> ct_make_expanded() {
    std::array<char, ct_compute_expanded_size<Tmpl, Entries...>() + 1> arr{};
    auto        sv  = nttp_string_view(Tmpl);
    std::size_t out = 0;
    std::size_t pos = 0;
    using table_t = ct_var_table<Entries...>;
    while (pos < sv.size()) {
      auto var_start     = constexpr_find(sv, "@var(", pos);
      auto partial_start = constexpr_find(sv, "{{>", pos);
      auto next          = (var_start == std::string_view::npos)        ? partial_start
                         : (partial_start == std::string_view::npos) ? var_start
                                                                   : std::min(var_start, partial_start);
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
        auto val  = table_t::lookup(name);
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
        auto val  = table_t::lookup(name);
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
  }

  template <auto Tmpl, typename T, auto... Entries>
  consteval auto ct_parse_expanded(std::string_view sv) {
    detail::ct_parse_context<ct_compute_expanded_size<Tmpl, Entries...>() + 1> ctx;
    detail::ct_parse_into(ctx, sv);
    return detail::resolve_field_indices<T>(ctx.tmpl);
  }

  // ---- constexpr 計算を保持する thin-wrapper 用構造体 ----

  template <auto Tmpl, bool Trim, bool Lstrip, typename T, typename PartialSet = ct_partials<>>
  struct nttp_render_data {
    static constexpr std::string_view tmpl_sv  = nttp_string_view(Tmpl);
    static constexpr auto             parsed   = detail::parse_fixed_impl<Tmpl, Trim, Lstrip>();
    static constexpr auto             resolved = detail::resolve_field_indices<T>(parsed);
    static constexpr auto             ct_bc    = detail::ct_chunks_to_bytecode<T>(resolved);
    using partial_set = PartialSet;  ///< 外部レジストリ（ct_partials<...>） */
  };

  // ct_partials<Pairs...> から partial_entries を取り出すトレイト
  template <typename T, fixed_string... Pairs>
  std::vector<partial_entry> const& ct_partial_entries();

  template <typename T, typename Set>
  struct ct_partial_entries_trait;
  template <typename T, fixed_string... Pairs>
  struct ct_partial_entries_trait<T, ct_partials<Pairs...>> {
    static std::vector<partial_entry> const& get() { return detail::ct_partial_entries<T, Pairs...>(); }
  };



  template <auto Tmpl, typename T, auto... Entries>
  struct nttp_atvar_data {
    static constexpr std::size_t expanded_size = detail::ct_compute_expanded_size<Tmpl, Entries...>();
    static constexpr auto             data          = detail::ct_make_expanded<Tmpl, Entries...>();
    static constexpr auto             parsed        = detail::ct_parse_expanded<Tmpl, T, Entries...>(std::string_view{data.data(), expanded_size});
    static constexpr auto             ct_bc         = detail::ct_chunks_to_bytecode<T>(parsed);
  };

  /**
   * @brief ct_partials から bytecode::partial_entries を構築する
   *
   * @details vector / shared_ptr は constexpr 構築不可のため、static ローカル初期化
   *          （実行時）の中で bc_compile<T> を呼び出す。既存の
   *          nttp_partial_bytecode_holder と同じ戦略。前方参照不可のため登録順にコンパイル。
   *
   * @tparam T コンテキスト型
   * @tparam Pairs ct_partials の名前・本文パック（"n1","b1","n2","b2",...）
   * @return std::vector<partial_entry> 名前解決用の partial エントリ群
   */
  template <typename T, fixed_string... Pairs>
  std::vector<partial_entry> const& ct_partial_entries() {
    static auto const entries = [] {
      std::vector<partial_entry> v;
      std::string pending_name;
      auto handle = [&](auto fs) {
        std::string_view s = nttp_string_view(fs);
        if (pending_name.empty()) {
          pending_name.assign(s);
        } else {
          bc_compiler<T> compiler;
          compiler.set_partial_entries(v);
          auto pbc = compiler.compile(s);
          v.push_back({pending_name, std::make_shared<bytecode>(std::move(pbc))});
          pending_name.clear();
        }
      };
      (handle(Pairs), ...);
      return v;
    }();
    return entries;
  }

  template <typename Data>
  detail::bytecode const& nttp_bytecode_holder() {
    static detail::bytecode const bc = detail::to_bytecode(Data::ct_bc);
    return bc;
  }

  template <typename Data, typename T>
  detail::bytecode const& nttp_partial_bytecode_holder() {
    static auto const bc = [] {
      auto bc = detail::to_bytecode(Data::ct_bc);
      // partial_entries を partial_names[0..partial_total) の順に構築し、
      // call_partial のオペランド（名前インデックス）と一致させる。
      // [0, partial_count) は #partialdef 本体、[partial_count, partial_total) は外部 {{> }} 参照。
      bc.partial_entries.reserve(Data::parsed.partial_total);
      auto tmpl_sv = Data::tmpl_sv;
      for (std::size_t i = 0; i < Data::parsed.partial_count; ++i) {
        auto                   body = tmpl_sv.substr(Data::parsed.partial_body_starts[i], Data::parsed.partial_body_ends[i] - Data::parsed.partial_body_starts[i]);
        detail::bc_compiler<T> compiler;
        compiler.set_partial_entries(bc.partial_entries);
        auto partial_bc = compiler.compile(std::string(body));
        if (partial_bc.error.ec != error_code::none) {
          bc.error = partial_bc.error;
          break;
        }
        partial_entry e;
        e.name = std::string(Data::parsed.partial_names[i]);
        e.bc = std::make_shared<detail::bytecode>(std::move(partial_bc));
        e.local = Data::parsed.partial_local[i];
        bc.partial_entries.push_back(std::move(e));
      }
      // 外部レジストリ（ct_partials<...>）から、未定義の {{> }} 参照を名前解決して差し込む
      if constexpr (Data::partial_set::count > 0) {
        auto const& ext = detail::ct_partial_entries_trait<T, typename Data::partial_set>::get();
        for (std::size_t i = Data::parsed.partial_count; i < Data::parsed.partial_total; ++i) {
          auto const& name = Data::parsed.partial_names[i];
          auto it = std::find_if(ext.begin(), ext.end(), [&](auto const& e) { return e.name == name; });
          if (it == ext.end()) {
            bc.error = error_ctx{0, error_code::unknown_key, name};
            break;
          }
          bc.partial_entries.push_back(*it);
        }
      }
      return bc;
    }();
    return bc;
  }

  // 指定 partial から推移的に到達可能な partial を後行順 DFS で列挙。
  // order は依存先が先頭に来るトポロジカル順（実行時コンパイラは前方参照不可のため必須）。
  template <std::size_t N>
  struct partial_closure {
    std::array<std::size_t, N> order{};
    std::size_t count = 0;
    bool found = false;
  };

  template <std::size_t N>
  consteval partial_closure<N> compute_partial_closure(ct_parsed_template<N> const& p, std::string_view sv, std::string_view target) {
    partial_closure<N> result;
    std::size_t target_idx = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < p.partial_count; ++i) {
      if (p.partial_names[i] == target) {
        target_idx = i;
        break;
      }
    }
    if (target_idx == static_cast<std::size_t>(-1))
      return result;  // found == false
    result.found = true;

    std::array<bool, N> visited{};
    std::array<bool, N> in_stack{};
    auto dfs = [&](auto& self, std::size_t node) -> void {
      if (visited[node])
        return;
      visited[node] = true;
      in_stack[node] = true;
      std::string_view body = sv.substr(p.partial_body_starts[node], p.partial_body_ends[node] - p.partial_body_starts[node]);
      std::size_t pos = 0;
      while (pos < body.size()) {
        auto at = constexpr_find(body, "{{#partial ", pos);
        if (at == std::string_view::npos)
          break;
        auto end = constexpr_find(body, "}}", at);
        if (end == std::string_view::npos)
          break;
        std::string_view ref = trim_sv(body.substr(at + 11, end - (at + 11)));  // "{{#partial " は 11 文字
        for (std::size_t j = 0; j < p.partial_count; ++j) {
          if (p.partial_names[j] == ref) {
            if (!in_stack[j])
              self(self, j);
            break;
          }
        }
        pos = end + 2;
      }
      in_stack[node] = false;
      result.order[result.count++] = node;  // 後行順：依存先の後
    };
    dfs(dfs, target_idx);
    return result;
  }

  // ponytail: 循環参照（A→B→A）は visited で無限ループを回避するがトポ順が付かず、
  // 実行時コンパイルで unknown_key になる。前方参照不可という現行制約の範囲内。

  // 指定 partial とその推移的依存だけをコンパイルし、それ以外を byte コードから捨てる。
  template <typename Data, fixed_string PartialName, typename T>
  detail::bytecode const& nttp_selected_partial_holder() {
    static auto const bc = [] {
      constexpr auto target_sv = detail::nttp_string_view(PartialName);
      constexpr auto closure = detail::compute_partial_closure(Data::parsed, Data::tmpl_sv, target_sv);
      detail::bytecode bc;
      if constexpr (!closure.found) {
        bc.error = error_ctx{0, error_code::unknown_key, target_sv};
        return bc;
      }
      auto tmpl_sv = Data::tmpl_sv;
      for (std::size_t k = 0; k < closure.count; ++k) {
        std::size_t i = closure.order[k];  // 依存先が先
        auto body = tmpl_sv.substr(Data::parsed.partial_body_starts[i], Data::parsed.partial_body_ends[i] - Data::parsed.partial_body_starts[i]);
        detail::bc_compiler<T> compiler;
        compiler.set_partial_entries(bc.partial_entries);  // 既コンパイル済み依存先を渡す
        auto partial_bc = compiler.compile(std::string(body));
        if (partial_bc.error.ec != error_code::none) {
          bc.error = partial_bc.error;
          break;
        }
        bc.partial_entries.push_back({std::string(Data::parsed.partial_names[i]), std::make_shared<detail::bytecode>(std::move(partial_bc))});
      }
      return bc;
    }();
    return bc;
  }

  // fixed_string ではなく auto (FrozenString 等) の PartialName を受ける版。
  // 内部は上と同一。nttp_string_view が FrozenString を string_view に橋渡しする。
  template <typename Data, auto PartialName, typename T>
    requires (!is_fixed_string_type_v<decltype(PartialName)>)
  detail::bytecode const& nttp_selected_partial_holder() {
    static auto const bc = [] {
      constexpr auto target_sv = detail::nttp_string_view(PartialName);
      constexpr auto closure = detail::compute_partial_closure(Data::parsed, Data::tmpl_sv, target_sv);
      detail::bytecode bc;
      if constexpr (!closure.found) {
        bc.error = error_ctx{0, error_code::unknown_key, target_sv};
        return bc;
      }
      auto tmpl_sv = Data::tmpl_sv;
      for (std::size_t k = 0; k < closure.count; ++k) {
        std::size_t i = closure.order[k];
        auto body = tmpl_sv.substr(Data::parsed.partial_body_starts[i], Data::parsed.partial_body_ends[i] - Data::parsed.partial_body_starts[i]);
        detail::bc_compiler<T> compiler;
        compiler.set_partial_entries(bc.partial_entries);
        auto partial_bc = compiler.compile(std::string(body));
        if (partial_bc.error.ec != error_code::none) {
          bc.error = partial_bc.error;
          break;
        }
        bc.partial_entries.push_back({std::string(Data::parsed.partial_names[i]), std::make_shared<detail::bytecode>(std::move(partial_bc))});
      }
      return bc;
    }();
    return bc;
  }

}  // namespace detail

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
template <fixed_string Tmpl, typename Reg, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
  requires detail::is_ct_partials_v<Reg>
[[nodiscard]] expected<std::string> render(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T, Reg>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
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
template <fixed_string Tmpl, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
[[nodiscard]] expected<void> render(T const& value, std::string& out) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into(detail::nttp_partial_bytecode_holder<D, T>(), value, out);
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

#if INJAMM_HAS_FROZENCHARS

template <auto Tmpl, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)>)
[[nodiscard]] expected<std::string> render(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute(detail::nttp_partial_bytecode_holder<D, T>(), value);
}

template <auto Tmpl, typename Reg, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)> && detail::is_ct_partials_v<Reg>)
[[nodiscard]] expected<std::string> render(T const& value) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T, Reg>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute(detail::nttp_partial_bytecode_holder<D, T>(), value);
}

template <auto Tmpl, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)>)
[[nodiscard]] expected<void> render(T const& value, std::string& out) {
  using D = detail::nttp_render_data<Tmpl, TrimBlocks != 0, LstripBlocks != 0, T>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into(detail::nttp_partial_bytecode_holder<D, T>(), value, out);
}

template <auto Tmpl, fixed_string... Entries, typename T>
  requires(sizeof...(Entries) > 0 && !detail::is_fixed_string_type_v<decltype(Tmpl)> && (detail::is_fixed_string_type_v<decltype(Entries)> && ...))
[[nodiscard]] expected<std::string> render(T const& value) {
  static_assert(sizeof...(Entries) % 2 == 0, "injamm: @var entries must be key-value pairs (even count). "
                                             "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data)");
  using D = detail::nttp_atvar_data<Tmpl, T, Entries...>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute(detail::nttp_bytecode_holder<D>(), value);
}

template <auto Tmpl, fixed_string... Entries, typename T>
  requires(sizeof...(Entries) > 0 && !detail::is_fixed_string_type_v<decltype(Tmpl)> && (detail::is_fixed_string_type_v<decltype(Entries)> && ...))
[[nodiscard]] expected<void> render(T const& value, std::string& out) {
  static_assert(sizeof...(Entries) % 2 == 0, "injamm: @var entries must be key-value pairs (even count). "
                                             "Example: render<tmpl, \"key1\", \"value1\", \"key2\", \"value2\">(data, out)");
  using D = detail::nttp_atvar_data<Tmpl, T, Entries...>;
  if constexpr (D::ct_bc.error.ec != error_code::none)
    return std::unexpected(D::ct_bc.error);
  return detail::bc_execute_into(detail::nttp_bytecode_holder<D>(), value, out);
}

// FrozenString 等の auto Entries を受ける版（文字列リテラルは fixed_string... 版が選ばれる）。
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
template <fixed_string Tmpl, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
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
template <fixed_string Tmpl, typename Reg, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
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
template <fixed_string Tmpl, fixed_string PartialName, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
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

#if INJAMM_HAS_FROZENCHARS

/**
 * @brief NTTP ベースの名前付き partial レンダリング（FrozenString テンプレート対応）
 *
 * @details render の auto Tmpl オーバーロードと同様、frozenchars::FrozenString
 *          (_fs リテラル) をテンプレート文字列として受け取れる。fixed_string 版と
 *           overload セットを分けるため、Tmpl が fixed_string でない場合のみ選択される。
 */
template <auto Tmpl, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
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

template <auto Tmpl, typename Reg, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
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
template <auto Tmpl, fixed_string PartialName, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)>)
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

/**
 * @brief NTTP ベースの名前付き partial レンダリング（partial 名をテンプレート引数で指定、FrozenString 対応）
 */
template <auto Tmpl, auto PartialName, int TrimBlocks = 0, int LstripBlocks = 0, typename T>
  requires (!detail::is_fixed_string_type_v<decltype(Tmpl)> && !detail::is_fixed_string_type_v<decltype(PartialName)>)
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

#endif

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
[[nodiscard]] auto bind(Containers const&... values) -> detail::bound_context<detail::name_list<Names...>, Containers...> {
  static_assert(sizeof...(Names) == sizeof...(Containers), "injamm: bind() requires the same number of names and containers. "
                                                           "Example: bind<\"items\", \"user\">(items, user)");
  return detail::bound_context<detail::name_list<Names...>, Containers...>{std::forward_as_tuple(values...)};
}

template <typename T>
[[nodiscard]] auto bind(T const& value) -> detail::bound_context<detail::name_list<fixed_string{"_"}>, T> {
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
  explicit engine(std::string_view tmpl, bool trim_blocks = false, bool lstrip_blocks = false) : bc_(detail::bc_compile<T>(tmpl, trim_blocks, lstrip_blocks)) {}

  template <class ConstMap>
  explicit engine(std::string_view tmpl, ConstMap const& consts, bool trim_blocks = false, bool lstrip_blocks = false) : bc_(detail::bc_compile<T>(tmpl, consts, trim_blocks, lstrip_blocks)) {}

  /**
   * @brief テンプレート文字列と登録済み partial から構築（実行時コンパイル）
   *
   * @param tmpl テンプレート文字列（std::string_view）
   * @param partials 外部から注入する名前付き partial のリスト（{{> name}} 用）
   * @param trim_blocks 閉じタグ後の改行を除去する（デフォルト false）
   * @param lstrip_blocks ブロックタグ前の空白を除去する（デフォルト false）
   */
  explicit engine(std::string_view tmpl, std::vector<detail::partial_entry> partials, bool trim_blocks = false, bool lstrip_blocks = false) : bc_(detail::bc_compile<T>(tmpl, std::move(partials), trim_blocks, lstrip_blocks)) {}

  /**
   * @brief プリコンパイル済みバイトコードから構築（デシリアライズ用）
   *
   * @details save_bytecode() で保存されたバイトコードを読み込んだ結果を直接渡す。
   *          コンパイル済みのバイトコードをそのまま利用するため、テンプレート文字列の
   *          パース/コンパイルを行わない。
   *
   * @param bc プリコンパイル済みバイトコード
   */
  explicit engine(detail::bytecode bc) : bc_(std::move(bc)) {}

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
    auto it = std::find_if(bc_.partial_entries.begin(), bc_.partial_entries.end(), [&](auto const& e) { return !e.local && e.name == partial_name; });
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
  [[nodiscard]] std::string disassemble() const { return bc_.disassemble(); }

  /**
   * @brief 内部バイトコードへの const 参照を取得する
   *
   * @details save_bytecode() に渡してシリアライズしたり、逆アセンブル結果を
   *          取得したりするために内部のバイトコードを公開する。
   *
   * @return detail::bytecode const& 内部バイトコードへの const 参照
   */
  [[nodiscard]] detail::bytecode const& get_bytecode() const { return bc_; }
};

/**
 * @brief 名前付き partial エントリを構築する（{{> name}} レジストリ用）
 *
 * @tparam T コンテキスト型（glz::meta<T> 要特殊化）
 * @param name partial 名
 * @param body partial 本文のテンプレート文字列
 * @return detail::partial_entry engine コンストラクタへ渡すエントリ
 */
template <class T>
[[nodiscard]] detail::partial_entry make_partial(std::string name, std::string_view body,
                                                 bool trim_blocks = false, bool lstrip_blocks = false) {
  return detail::partial_entry{std::move(name),
                                std::make_shared<detail::bytecode>(detail::bc_compile<T>(body, trim_blocks, lstrip_blocks))};
}

// CT 用 partial レジストリ（型パック）を公開 API として露出
using detail::ct_partials;

}  // namespace injamm
