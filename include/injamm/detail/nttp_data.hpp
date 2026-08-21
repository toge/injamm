#pragma once

/**
 * @file nttp_data.hpp
 * @brief NTTP コンパイル時データ構築ヘルパ（detail）
 * @details escape_hatch.hpp から分割。CT パース・@var 展開・partial 依存解析・
 *          bytecode キャッシュ構築の constexpr / static キャッシュ層のみを収容。
 *          公開 API (render/engine) は含まない。
 */

#include <array>
#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../types.hpp"
#include "../ct_chunk.hpp"
#include "../ct_parse.hpp"
#include "../bytecode.hpp"
#include "../bytecode_compile.hpp"
#include "../bytecode_ct_compile.hpp"

#if __has_include(<frozenchars/mod/core.hpp>)
#include <frozenchars/mod/core.hpp>
#ifndef INJAMM_HAS_FROZENCHARS
#define INJAMM_HAS_FROZENCHARS 1
#endif
#endif

namespace injamm {

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
        auto close_tag = constexpr_find_close_partialdef(sv, tag_end + 2);
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

  /**
   * @brief 指定 partialdef 本文のコンパイル時パース（直線 only 限定版）
   *
   * @details メインテンプレート中で {{#partialdef name}}...{{/partialdef}} により定義された
   *          partial 本文をコンパイル時（constexpr）にパースし、直線アンロール実行
   *          （ct_executor）に必要な ct_bc を構築する。htmx の行更新など「直線のみの
   *          partial」を毎回実行時コンパイルせずに高速レンダリングするのが目的。
   *          section 等を含む場合 ct_bc にブロック命令が現れるため ct_is_unrollable が
   *          false になり、呼び出し側で既存の実行時 VM にフォールバックされる。
   *
   * @tparam Tmpl      メインテンプレート文字列（NTTP）
   * @tparam BodyStart 本文開始オフセット（partialdef 開始タグの直後）
   * @tparam BodyEnd   本文終了オフセット（{{/partialdef}} の開始位置）
   * @tparam Trim      引数テンプレート側の trim_blocks
   * @tparam Lstrip    引数テンプレート側の lstrip_blocks
   * @tparam T         コンテキスト型（glz::meta<T> 要特殊化）
   */
  template <auto Tmpl, std::size_t BodyStart, std::size_t BodyEnd, bool Trim, bool Lstrip, typename T>
  struct nttp_partial_body_data {
    static constexpr auto parsed = [] {
      constexpr std::string_view sv   = nttp_string_view(Tmpl);
      constexpr std::string_view body = sv.substr(BodyStart, BodyEnd - BodyStart);
      ct_parse_context<body.size() + 1> ctx;
      ct_parse_into(ctx, body, Trim, Lstrip);
      return ctx.tmpl;
    }();
    static constexpr auto resolved = detail::resolve_field_indices<T>(parsed);
    static constexpr auto ct_bc    = detail::ct_chunks_to_bytecode<T>(resolved);
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
      // 前方参照に対応するため、先に全エントリを名前のみ登録（bc は後で埋める）
      for (std::size_t i = 0; i < Data::parsed.partial_count; ++i) {
        bc.partial_entries.push_back({std::string(Data::parsed.partial_names[i]), {}, Data::parsed.partial_local[i]});
      }
      // トポロジカル順（依存先が先）でボディをコンパイル
      {
        std::array<std::size_t, Data::parsed.partial_count> order{};
        std::size_t order_count = 0;
        {
          std::array<bool, Data::parsed.partial_count> visited{};
          std::array<bool, Data::parsed.partial_count> in_stack{};
          auto dfs = [&](auto& self, std::size_t node) -> void {
            if (visited[node]) return;
            visited[node] = true;
            in_stack[node] = true;
            std::string_view body = tmpl_sv.substr(Data::parsed.partial_body_starts[node], Data::parsed.partial_body_ends[node] - Data::parsed.partial_body_starts[node]);
            std::size_t pos = 0;
            while (pos < body.size()) {
              auto at = constexpr_find(body, "{{#partial ", pos);
              if (at == std::string_view::npos) break;
              auto end = constexpr_find(body, "}}", at);
              if (end == std::string_view::npos) break;
              std::string_view ref = trim_sv(body.substr(at + 11, end - (at + 11)));
              for (std::size_t j = 0; j < Data::parsed.partial_count; ++j) {
                if (Data::parsed.partial_names[j] == ref) {
                  if (!in_stack[j]) self(self, j);
                  break;
                }
              }
              pos = end + 2;
            }
            in_stack[node] = false;
            order[order_count++] = node;
          };
          for (std::size_t i = 0; i < Data::parsed.partial_count; ++i)
            dfs(dfs, i);
        }
        for (std::size_t k = 0; k < order_count; ++k) {
          std::size_t i = order[k];
          auto body = tmpl_sv.substr(Data::parsed.partial_body_starts[i], Data::parsed.partial_body_ends[i] - Data::parsed.partial_body_starts[i]);
          detail::bc_compiler<T> compiler;
          compiler.set_partial_entries(bc.partial_entries);
          auto partial_bc = compiler.compile(std::string(body));
          if (partial_bc.error.ec != error_code::none) {
            bc.error = partial_bc.error;
            break;
          }
          bc.partial_entries[i].bc = std::make_shared<detail::bytecode>(std::move(partial_bc));
        }
      }
      // ネストした partial 定義（コンパイラ内で抽出される）を bc.partial_entries に昇格させる。
      // これにより render_partial(t, "inner") で nttp_partial_bytecode_holder のキャッシュから
      // アクセス可能になる。
      for (std::size_t ei = 0; ei < bc.partial_entries.size(); ++ei) {
        auto const& entry = bc.partial_entries[ei];
        if (!entry.bc) continue;
        for (auto const& nested : entry.bc->partial_entries) {
          if (nested.local) continue;
          auto dup = std::find_if(bc.partial_entries.begin(), bc.partial_entries.end(),
                                  [&](auto const& e) { return e.name == nested.name; });
          if (dup == bc.partial_entries.end())
            bc.partial_entries.push_back(nested);
        }
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
    // ネストした partial のボディ範囲（count == 0 かつ found の場合に有効）
    std::size_t nested_body_start = 0;
    std::size_t nested_body_end   = 0;
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
    if (target_idx == static_cast<std::size_t>(-1)) {
      // partial_names に見つからない → テンプレート全体からネスト {{#partialdef}} を探索
      std::size_t pos = 0;
      while (pos < sv.size()) {
        auto pdef = constexpr_find(sv, "{{#partialdef ", pos);
        if (pdef == std::string_view::npos) break;
        auto tag_end = constexpr_find(sv, "}}", pdef);
        if (tag_end == std::string_view::npos) break;
        auto inner = trim_sv(sv.substr(pdef + 2, tag_end - pdef - 2));
        if (inner.starts_with("#partialdef ")) {
          auto name = trim_sv(inner.substr(12));
          auto sp = constexpr_find(name, ' ');
          if (sp != std::string_view::npos)
            name = trim_sv(name.substr(0, sp));
          if (name == target) {
            auto close_tag = constexpr_find_close_partialdef(sv, tag_end + 2);
            if (close_tag != std::string_view::npos) {
              result.found = true;
              result.nested_body_start = tag_end + 2;
              result.nested_body_end   = close_tag;
              return result;  // count == 0, 依存なしの leaf として扱う
            }
          }
        }
        pos = tag_end + 2;
      }
      return result;  // found == false
    }
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
  // fixed_string / FrozenString 両対応: nttp_string_view が橋渡しするため単一 auto テンプレートで足りる。
  template <typename Data, auto PartialName, typename T>
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
      if constexpr (closure.count == 0) {
        // ネストした partial（トップレベル partial_names に含まれない）
        auto body = tmpl_sv.substr(closure.nested_body_start, closure.nested_body_end - closure.nested_body_start);
        detail::bc_compiler<T> compiler;
        auto partial_bc = compiler.compile(std::string(body));
        if (partial_bc.error.ec != error_code::none) {
          bc.error = partial_bc.error;
          return bc;
        }
        bc.partial_entries.push_back({std::string(target_sv), std::make_shared<detail::bytecode>(std::move(partial_bc))});
        return bc;
      }
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

}  // namespace detail

}  // namespace injamm
