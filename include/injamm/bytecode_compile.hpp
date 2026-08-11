#pragma once

#include <string_view>
#include <type_traits>

#include "bytecode.hpp"
#include "enum_io.hpp"
#include "filters.hpp"
#include "glz_dispatch.hpp"
#include "parse.hpp"
// enum_io.hpp provides enum_name_to_int and serialize_enum

namespace injamm::detail {

/**
 * @brief コンパイル時コンテキスト型追跡用の型消去オペレーション
 * @details セクション本体をコンパイルする際、実行時コンテキストとなる要素型で
 *          フィールドインデックスを事前解決するために使用する。
 *          型を関数ポインタに消去することで bc_compiler 本体のテンプレート化を避ける。
 */
struct compile_ctx_ops {
  /** @brief 現在のコンテキスト型でフィールド名→インデックスを解決する */
  std::uint32_t (*resolve)(std::string_view);
  /** @brief セクションキーに対する本体コンテキストの ops を返す */
  compile_ctx_ops (*section_child)(std::string_view);
  /** @brief ドット区切りパスの階層別フィールドインデックスを解決する */
  void (*resolve_path)(std::string_view, std::vector<std::uint32_t>&);
  /** @brief enum フィールドの列挙子名→整数解決（if 比較用） */
  std::optional<long long> (*enum_lookup)(std::uint32_t, std::string_view);
  /** @brief コンテキスト型がコンパイル時に既知か（false なら resolve 失敗を根拠にした最適化は不可） */
  bool known = false;
};

/** @brief 何も解決できないコンテキスト（型不明時のフォールバック） */
inline compile_ctx_ops null_compile_ctx_ops() {
  return {
      [](std::string_view) -> std::uint32_t { return UINT32_MAX; },
      [](std::string_view) { return null_compile_ctx_ops(); },
      [](std::string_view, std::vector<std::uint32_t>&) {},
      [](std::uint32_t, std::string_view) { return std::optional<long long>{}; },
      false,
  };
}

/** @brief glaze リフレクションでフィールド名→インデックスを解決する（型消去版の実体） */
template <class V>
std::uint32_t compile_ctx_resolve_field(std::string_view key) {
  if constexpr (ct_glz_reflectable<V>) {
    constexpr auto sz = static_cast<std::size_t>(glz::reflect<V>::size);
    for (std::size_t i = 0; i < sz; ++i) {
      if (std::string_view{glz::reflect<V>::keys[i]} == key) {
        return static_cast<std::uint32_t>(i);
      }
    }
  }
  return UINT32_MAX;
}

/** @brief フィールドインデックスからフィールド型で関数を呼ぶ（型消去版の実体） */
template <class V, class F>
void compile_ctx_with_field_type(std::uint32_t field_idx, F&& fn) {
  if (field_idx == UINT32_MAX) {
    return;
  }
  if constexpr (ct_glz_reflectable<V>) {
    constexpr auto sz = static_cast<std::size_t>(glz::reflect<V>::size);
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      (([&] {
         if (I == field_idx) {
           using tied_t = decltype(glz::to_tie(std::declval<V const&>()));
           using FT     = std::remove_cvref_t<decltype(glz::get<I>(std::declval<tied_t>()))>;
           fn.template operator()<FT>();
         }
       }()),
       ...);
    }(std::make_index_sequence<sz>{});
  }
}

template <class V>
compile_ctx_ops make_compile_ctx_ops();

/**
 * @brief セクションキーに対する本体コンテキスト型の ops を計算する
 * @details 分岐順は bc_executor の emit_section 処理と一致させること。
 *          コンテナ系は要素型、bool/string は同一コンテキスト、
 *          reflectable フィールド走査はフィールドごとに型が変わるため解決不能とする。
 */
template <class V>
compile_ctx_ops compile_ctx_section_child(std::string_view key) {
  auto result = null_compile_ctx_ops();
  if constexpr (ct_glz_reflectable<V>) {
    auto const fidx = compile_ctx_resolve_field<V>(key);
    compile_ctx_with_field_type<V>(fidx, [&]<class FT>() {
      if constexpr (ct_is_vector_like<FT>) {
        result = make_compile_ctx_ops<typename FT::value_type>();
      } else if constexpr (std::same_as<FT, bool> || std::same_as<FT, std::string> ||
                           std::same_as<FT, std::string_view> || char_pointer_v<FT>) {
        result = make_compile_ctx_ops<V>();
      } else if constexpr (is_std_optional_v<FT>) {
        result = make_compile_ctx_ops<typename FT::value_type>();
      } else if constexpr (ct_is_map_like<FT>) {
        result = make_compile_ctx_ops<typename FT::mapped_type>();
      } else if constexpr (ct_is_set_like<FT>) {
        result = make_compile_ctx_ops<typename FT::value_type>();
      } else if constexpr (forward_iterable<FT>) {
        result = make_compile_ctx_ops<typename FT::value_type>();
      }
    });
  }
  return result;
}

/**
 * @brief ドット区切りパスの階層別フィールドインデックスを型チェーンに沿って解決する
 * @details 各階層でセグメント名をフィールドインデックスに変換し out に追記する。
 *          解決できない階層（ベクタ添字・型不明）に達したらそこで打ち切る。
 *          実行時は名前検証付きで使用されるため、途中までの解決でも安全。
 */
template <class V>
void compile_ctx_resolve_path(std::string_view path, std::vector<std::uint32_t>& out) {
  if constexpr (ct_glz_reflectable<V>) {
    auto const dot = path.find('.');
    auto const seg = path.substr(0, dot);
    auto const idx = compile_ctx_resolve_field<V>(seg);
    out.push_back(idx);
    if (dot == std::string_view::npos || idx == UINT32_MAX) {
      return;
    }
    compile_ctx_with_field_type<V>(idx, [&]<class FT>() {
      if constexpr (ct_glz_reflectable<FT>) {
        compile_ctx_resolve_path<FT>(path.substr(dot + 1), out);
      }
    });
  }
}

/** @brief enum フィールドの列挙子名→整数解決（型消去版の実体） */
template <class V>
std::optional<long long> compile_ctx_enum_lookup(std::uint32_t field_idx, std::string_view name) {
  std::optional<long long> result;
  compile_ctx_with_field_type<V>(field_idx, [&]<class FT>() {
    if constexpr (std::is_enum_v<FT>) {
      if (auto ev = enum_name_to_int<FT>(name)) {
        result = static_cast<long long>(*ev);
      }
    }
  });
  return result;
}

/** @brief 型 V のコンテキスト ops を生成する
 *  @details known は「コンパイル時の解決失敗＝実行時の走査失敗」が保証される型のみ true。
 *           runtime_field_accessible な型は実行時 find() で任意キーを解決しうるため false。 */
template <class V>
compile_ctx_ops make_compile_ctx_ops() {
  return {&compile_ctx_resolve_field<V>, &compile_ctx_section_child<V>, &compile_ctx_resolve_path<V>,
          &compile_ctx_enum_lookup<V>, !runtime_field_accessible<V>};
}

template <class Emitter>
void emit_filter_chain(Emitter&& emit, std::vector<string_filter_entry> const& filters,
                       std::vector<int_filter_entry> const& int_filters,
                       std::vector<float_filter_entry> const& float_filters) {
  for (auto f : filters) {
    // 汎用 filter_string: operand = arg1, operand2 = string_filter 種別, operand3 = 未使用 (UINT32_MAX)
    // 文字列引数は stabilize_filter_strings が bc_.literals にコピー済みで、
    // 実行時は var_ref.filters を直接使うため operand3 は不要。
    auto const no_str = UINT32_MAX;
    switch (f.filter) {
      case string_filter::upper:      emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::upper), no_str); break;
      case string_filter::lower:      emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::lower), no_str); break;
      case string_filter::capitalize: emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::capitalize), no_str); break;
      case string_filter::title:      emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::title), no_str); break;
      case string_filter::trim:       emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::trim), no_str); break;
      case string_filter::ltrim:      emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::ltrim), no_str); break;
      case string_filter::rtrim:      emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::rtrim), no_str); break;
      case string_filter::left:       emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::left), no_str); break;
      case string_filter::right:      emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::right), no_str); break;
      case string_filter::center:     emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::center), no_str); break;
      case string_filter::truncate:   emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::truncate), no_str); break;
      case string_filter::substr:     emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::substr), static_cast<std::uint32_t>(f.arg2)); break;
      case string_filter::replace:
        emit(bc_opcode::filter_string, 0, static_cast<std::uint32_t>(string_filter::replace), no_str);
        break;
      case string_filter::default_value:
        emit(bc_opcode::filter_string, 0, static_cast<std::uint32_t>(string_filter::default_value), no_str);
        break;
      case string_filter::to_json:
        emit(bc_opcode::filter_string, 0, static_cast<std::uint32_t>(string_filter::to_json), no_str);
        break;
      case string_filter::safe:
        emit(bc_opcode::filter_string, 0, static_cast<std::uint32_t>(string_filter::safe), no_str);
        break;
      case string_filter::indent:     emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::indent), no_str); break;
      case string_filter::pad:        emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::pad), no_str); break;
      case string_filter::pluralize:  emit(bc_opcode::filter_string, 0, static_cast<std::uint32_t>(string_filter::pluralize), no_str); break;
      case string_filter::format:     emit(bc_opcode::filter_string, 0, static_cast<std::uint32_t>(string_filter::format), no_str); break;
      case string_filter::repeat:     emit(bc_opcode::filter_string, static_cast<std::uint32_t>(f.arg1), static_cast<std::uint32_t>(string_filter::repeat), no_str); break;
    }
  }
  for (auto f : int_filters) {
    // 汎用 filter_int: operand = arg, operand2 = int_filter 種別
    emit(bc_opcode::filter_int, static_cast<std::uint32_t>(f.arg), static_cast<std::uint32_t>(f.filter), 0);
  }
  for (auto f : float_filters) {
    // 汎用 filter_float: operand = arg, operand2 = float_filter 種別
    emit(bc_opcode::filter_float, static_cast<std::uint32_t>(f.arg), static_cast<std::uint32_t>(f.filter), 0);
  }
}

/**
 * @brief バイトコードコンパイラ
 * @tparam T コンテキスト型（glaze リフレクション対応）
 * @details テンプレート文字列をパースし、bc_template が実行可能なバイトコード列に変換する。
 *          セクション、if/else、@index/@first/@last、反転セクションをサポートする。
 *          コンパイル時に glaze::reflect を使ってフィールドインデックスを解決し、
 *          実行時のルックアップを高速化する。
 */
template <class T>
class bc_compiler {
  /** @brief 生成中のバイトコード */
  bytecode bc_;
  /** @brief コンパイル対象のテンプレート文字列 */
  std::string_view tmpl_;
  /** @brief コメント除去後のテンプレート文字列（所有権保持用） */
  std::string clean_tmpl_;
  /** @brief テンプレート文字列上の現在位置 */
  std::size_t pos_ = 0;
  /** @brief 閉じタグ後の改行を除去する */
  bool trim_blocks_ = false;
  /** @brief ブロックタグ前の空白を除去する */
  bool lstrip_blocks_ = false;
  /** @brief コンテキスト型スタック（末尾が現在のセクション本体コンテキスト） */
  std::vector<compile_ctx_ops> ctx_stack_{make_compile_ctx_ops<T>()};
  /** @brief 内包セクションのキー名スタック（ループ束縛の事前判定用） */
  std::vector<std::string> section_keys_;

  /**
   * @brief ループ束縛参照の事前判定を行い binding_first フラグを設定する
   * @details キーの先頭セグメントが内包セクション名と一致し、かつ現在の要素型に
   *          同名フィールドが存在しない場合、実行時のフィールド走査は必ず失敗して
   *          束縛解決にフォールバックする。これをコンパイル時に確定させ、
   *          さらにサブパスの field_index を要素型で事前解決する。
   */
  void mark_binding_first(std::uint32_t idx, std::string_view key) {
    if (section_keys_.empty()) {
      return;
    }
    auto first_seg = key.substr(0, key.find('.'));
    bool matches = false;
    for (auto const& sk : section_keys_) {
      if (sk == first_seg) {
        matches = true;
        break;
      }
    }
    if (!matches) {
      return;
    }
    /** 要素型が不明な場合は実行時のフィールド走査結果を予測できないため何もしない */
    if (!ctx_stack_.back().known) {
      return;
    }
    /** 要素型に同名フィールドがある場合は従来のフィールド優先順を維持する */
    if (ctx_stack_.back().resolve(first_seg) != UINT32_MAX) {
      return;
    }
    auto& ref = bc_.var_refs[idx];
    ref.binding_first = true;
    /** サブパス（items.name の name 部分）を現在の要素型で事前解決する */
    if (key.size() > first_seg.size() + 1) {
      auto sub = key.substr(first_seg.size() + 1);
      if (sub.find('.') == std::string_view::npos) {
        ref.field_index = ctx_stack_.back().resolve(sub);
      }
    }
  }

  /**
   * @brief 現在のコンテキスト型でフィールドインデックスを解決する
   * @details セクション本体では要素型で解決する。要素型で見つからない場合は
   *          ルート型フォールバック（実行時の root_value_ 探索）に合わせて
   *          ルート型でも解決を試みる。
   */
  std::uint32_t ctx_resolve(std::string_view key) const {
    auto idx = ctx_stack_.back().resolve(key);
    if (idx == UINT32_MAX && ctx_stack_.size() > 1) {
      idx = resolve_field_index<T>(key);
    }
    return idx;
  }

  /**
   * @brief 変数参照の field_index / path_indices を現在コンテキストで事前解決する
   * @details 単一キーは field_index、ドット区切りパスは階層別 path_indices を設定する。
   *          いずれも実行時に名前検証付きで使用されるため誤解決しても安全。
   */
  void resolve_ref_indices(std::uint32_t idx, std::string_view key) {
    auto field_idx = ctx_resolve(key);
    if (field_idx != UINT32_MAX) {
      bc_.set_field_index(idx, field_idx);
      return;
    }
    if (key.find('.') == std::string_view::npos) {
      return;
    }
    if (classify_special_var(key) != special_var_kind::none) {
      return;
    }
    std::vector<std::uint32_t> out;
    ctx_stack_.back().resolve_path(key, out);
    if (out.empty() || out[0] == UINT32_MAX) {
      /** 現在コンテキストで解決できない場合はルート型チェーンを試す（セクションの root 探索に一致） */
      out.clear();
      compile_ctx_resolve_path<T>(key, out);
      if (!out.empty() && out[0] == UINT32_MAX) {
        out.clear();
      }
    }
    /** インライン固定長配列（4階層まで）にコピーする */
    auto& ref = bc_.var_refs[idx];
    auto  n   = std::min<std::size_t>(out.size(), ref.path_indices.size());
    for (std::size_t i = 0; i < n; ++i) {
      ref.path_indices[i] = out[i];
    }
    ref.path_hint_len = static_cast<std::uint8_t>(n);
  }

  /** @brief compile_body_impl の戻り値型 */
  enum class body_result : int { close = 0, else_ = 1, eof = 2 };

  /**
   * @brief glaze リフレクションを用いてフィールドインデックスを解決する
   * @tparam V リフレクション対象の型
   * @param key フィールド名
   * @return フィールドインデックス（見つからない場合は UINT32_MAX）
   */
  template <class V>
  static std::uint32_t resolve_field_index(std::string_view key) {
    if constexpr (ct_glz_reflectable<V>) {
      constexpr auto sz = static_cast<std::size_t>(glz::reflect<V>::size);
      for (std::size_t i = 0; i < sz; ++i) {
        if (std::string_view{glz::reflect<V>::keys[i]} == key) {
          return static_cast<std::uint32_t>(i);
        }
      }
    }
    return UINT32_MAX;
  }

  /**
   * @brief glaze リフレクションを用いて、フィールドインデックスからフィールド型で関数を呼ぶ
   * @tparam V リフレクション対象の型
   * @tparam F `template<class FieldType>()` を受け取る関数型
   * @param field_idx フィールドインデックス（UINT32_MAX の場合は何もしない）
   * @param fn 各フィールド型で呼ばれるラムダ（`fn.template operator()<FieldType>()` 形式）
   * @details enum フィールドのコンパイル時型取得に使用する。
   */
  template <class V, class F>
  static void with_field_type_at(std::uint32_t field_idx, F&& fn) {
    if (field_idx == UINT32_MAX) return;
    if constexpr (ct_glz_reflectable<V>) {
      constexpr auto sz = static_cast<std::size_t>(glz::reflect<V>::size);
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
           if (I == field_idx) {
             /** std::declval を使い未評価コンテキストで型を取得（null 逆参照を避ける） */
             using tied_t = decltype(glz::to_tie(std::declval<V const&>()));
             using FT     = std::remove_cvref_t<decltype(glz::get<I>(std::declval<tied_t>()))>;
             fn.template operator()<FT>();
           }
         }()),
         ...);
      }(std::make_index_sequence<sz>{});
    }
  }

  /** @brief フィルタ文字列引数を bc_.literals へ移して string_view の安定性を確保
   *
   *  string_filter_entry::str_arg1/str_arg2 はテンプレート文字列の一部を指す
   *  string_view であり、bc_ の move 後はダングリングしうる。bc_.literals
   *  にコピーして string_view を再設定することで、move 後も有効な参照を維持する。
   */
  void stabilize_filter_strings(std::uint32_t var_ref_idx) {
    std::size_t count = 0;
    for (auto const& f : bc_.var_refs[var_ref_idx].filters) {
      if (!f.str_arg1.empty()) ++count;
      if (!f.str_arg2.empty()) ++count;
    }
    if (count == 0) return;
    bc_.literals.reserve(bc_.literals.size() + count);
    for (auto& f : bc_.var_refs[var_ref_idx].filters) {
      if (!f.str_arg1.empty()) f.str_arg1 = bc_.literals[bc_.add_literal(f.str_arg1)];
      if (!f.str_arg2.empty()) f.str_arg2 = bc_.literals[bc_.add_literal(f.str_arg2)];
    }
  }

  /**
   * @brief リテラル出力命令を発行する
   * @param lit 出力するリテラル文字列
   */
  void emit_literal(std::string_view lit) {
    if (lit.empty()) return;
    auto idx = bc_.add_literal(lit);
    bc_.add_instruction(bc_opcode::emit_literal, idx);
  }

  /**
   * @brief 変数参照命令を発行する
   * @details 直前が emit_literal の場合は融合命令 emit_litvar / emit_litvar_raw に置き換える。
   *          これにより実行時の命令デコード回数が削減される。
   *          フィルタが指定されている場合は resolve_filtered → filter_* → emit_filtered パスを使用する。
   * @param key 変数名
   * @param raw 生出力（エスケープなし）フラグ
   * @param filters 適用する文字列フィルタの列
   */
  void emit_var(std::string_view key, bool raw, std::vector<string_filter_entry> filters = {}, std::vector<int_filter_entry> int_filters = {}, std::vector<float_filter_entry> float_filters = {}) {
    auto idx = bc_.add_var_ref(key);
    resolve_ref_indices(idx, key);
    if (bc_.var_refs[idx].field_index == UINT32_MAX) {
      mark_binding_first(idx, key);
    }
    bc_.var_refs[idx].filters = filters;
    bc_.var_refs[idx].int_filters = int_filters;
    bc_.var_refs[idx].float_filters = float_filters;
    stabilize_filter_strings(idx);
    // safe/json/format filter detection
    bool has_safe = false;
    bool has_json = false;
    bool has_chrono_format = false;
    for (auto const& f : filters) {
      if (f.filter == string_filter::safe) { has_safe = true; }
      else if (f.filter == string_filter::to_json) { has_json = true; }
      else if (f.filter == string_filter::format) { has_chrono_format = true; }
    }
    bool use_raw = raw || has_safe;
    // filter_flags を設定（ホットパスのループ排除）
    std::uint8_t flags = 0;
    if (has_json) flags |= 1;
    if (has_chrono_format) flags |= 2;
    // フィルタの有無で分岐
    if (filters.empty() && int_filters.empty() && float_filters.empty()) {
      // 既存の高速パス（変更なし）
      if (!bc_.instructions.empty()) {
        auto& last = bc_.instructions.back();
        if (last.op == bc_opcode::emit_literal) {
          last.op = use_raw ? bc_opcode::emit_litvar_raw : bc_opcode::emit_litvar;
          last.operand2 = idx;
          return;
        }
      }
      bc_.add_instruction(use_raw ? bc_opcode::emit_var_raw : bc_opcode::emit_var, idx);
    } else {
      // フィルタ専用パス: 後続フィルタ命令数を operand に格納（executor がスキップ用に使用）
      bc_.var_refs[idx].filter_flags = flags;
      auto filter_count = static_cast<std::uint32_t>(filters.size() + int_filters.size() + float_filters.size());
      bc_.add_instruction(bc_opcode::resolve_filtered, filter_count, idx);
      emit_filter_chain([this](bc_opcode op, std::uint32_t a = 0, std::uint32_t a2 = 0, std::uint32_t a3 = 0) {
        bc_.add_instruction(op, a, a2, a3);
      }, filters, int_filters, float_filters);
      bc_.add_instruction(use_raw ? bc_opcode::emit_filtered_raw : bc_opcode::emit_filtered);
    }
  }

  /**
   * @brief root.field 参照命令を発行する
   * @param key root.xxx 形式のキー全体
   * @param raw 生出力フラグ
   * @details プレフィックス root. を除去し、残りのパスを var_ref として登録して
   *          emit_at_root_field 命令を発行する。
   */
  void emit_root_field(std::string_view key, bool raw) {
    auto rest = key.substr(5);
    auto idx = bc_.add_var_ref(rest);
    auto field_idx = resolve_field_index<T>(rest);
    if (field_idx != UINT32_MAX) {
      bc_.set_field_index(idx, field_idx);
    }
    bc_.add_instruction(raw ? bc_opcode::emit_at_root_field_raw : bc_opcode::emit_at_root_field, idx);
  }

  /**
   * @brief loop.index/loop.is_first/loop.is_last/loop.key 参照命令を発行する
   * @param key loop.index / loop.is_first / loop.is_last / loop.key のいずれか
   */
  void emit_at_var(std::string_view key) {
    auto k = parse_loop_kind(key);
    if (!k) {
      return;
    }
    switch (*k) {
      case at_var_kind::index:
        bc_.add_instruction(bc_opcode::emit_at_index);
        break;
      case at_var_kind::index1:
        bc_.add_instruction(bc_opcode::emit_at_index1);
        break;
      case at_var_kind::size:
        bc_.add_instruction(bc_opcode::emit_at_size);
        break;
      case at_var_kind::first:
        bc_.add_instruction(bc_opcode::emit_at_first);
        break;
      case at_var_kind::last:
        bc_.add_instruction(bc_opcode::emit_at_last);
        break;
      case at_var_kind::key:
        bc_.add_instruction(bc_opcode::emit_at_key);
        break;
    }
  }

  /**
   * @brief 変数の要素数参照命令を発行する ({{field.size}})
   * @param key 変数名（末尾の .size は除去済み）
   * @param raw 生出力フラグ
   */
  void emit_var_size(std::string_view key, bool raw) {
    auto idx = bc_.add_var_ref(key);
    resolve_ref_indices(idx, key);
    bc_.add_instruction(bc_opcode::emit_var_size, idx);
  }

  /**
   * @brief セクション（配列ループ）をコンパイルする
   * @param key セクション変数名
   * @details {{#section}}...{{/section}} の構文を emit_section / emit_end 命令に変換する。
   *          セクション命令には /section の次の命令位置がジャンプ先として書き込まれる。
   */
  void compile_section(std::string_view key) {
    /** セクションキーをパイプ分割し reverse/take フィルタを検出する（ベースキー = parts[0]） */
    auto parts     = split_by_pipe(key);
    key            = parts[0];
    section_filter_entry fl;
    for (std::size_t fi = 1; fi < parts.size(); ++fi) {
      auto sf = parse_section_filter(parts[fi]);
      if (!sf) {
        bc_.error = error_ctx{pos_, error_code::unknown_filter, parts[fi]};
        return;
      }
      fl.reverse |= sf->reverse;
      if (sf->take) fl.take = sf->take;
    }

    auto idx = bc_.add_var_ref(static_cast<std::string>(key));
    bc_.var_refs[idx].section_reverse = fl.reverse ? 1 : 0;
    bc_.var_refs[idx].section_take    = fl.take ? static_cast<std::uint32_t>(*fl.take) + 1u : 0;  // N+1 エンコード（design amendment opt A）
    resolve_ref_indices(idx, key);
    bc_.add_instruction(bc_opcode::emit_section, 0, idx);

    /** @brief 後でジャンプ先を書き込むための命令位置を記録 */
    auto section_instr_idx = bc_.current_offset() - 1;

    /** セクション本体は要素型コンテキストでコンパイルする（else 本体は親コンテキスト） */
    ctx_stack_.push_back(ctx_stack_.back().section_child(key));
    section_keys_.emplace_back(key);
    bool reached_end = false;
    auto result = compile_body_impl(reached_end);
    section_keys_.pop_back();
    ctx_stack_.pop_back();

    if (result == body_result::else_) {
      // Section has {{else}}. Emit trampoline jump past else body.
      auto jump_instr = static_cast<std::uint32_t>(bc_.current_offset());
      bc_.add_instruction(bc_opcode::emit_else, 0, 0);

      // Patch operand3 = first else body instruction
      bc_.instructions[section_instr_idx].operand3 =
          static_cast<std::uint32_t>(bc_.current_offset());

      // Compile else body (until close tag)
      bool else_reached_end = false;
      auto else_result = compile_body_impl(else_reached_end);
      if (else_result == body_result::eof) {
        bc_.error = error_ctx{section_instr_idx, error_code::unexpected_end, key};
        return;
      }

      bc_.add_instruction(bc_opcode::emit_end);
      auto body_end = static_cast<std::uint32_t>(bc_.current_offset());
      bc_.patch_jump(section_instr_idx, body_end);
      bc_.patch_jump(jump_instr, body_end);

    } else if (result == body_result::close) {
      // No else — existing path
      bc_.add_instruction(bc_opcode::emit_end);
      bc_.patch_jump(section_instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
    } else {
      bc_.error = error_ctx{section_instr_idx, error_code::unexpected_end, key};
      return;
    }

    if (trim_blocks_ && pos_ < tmpl_.size() && tmpl_[pos_] == '\n') ++pos_;
  }

  /**
   * @brief 反転セクション（^section）をコンパイルする
   * @param key セクション変数名
   * @details {{^section}}...{{/section}} は変数が偽/空の場合に本体が描画される。
   */
  void compile_inverted(std::string_view key) {
    auto idx = bc_.add_var_ref(key);
    resolve_ref_indices(idx, key);
    bc_.add_instruction(bc_opcode::emit_inverted, 0, idx);

    auto section_instr_idx = bc_.current_offset() - 1;

    bool reached_end = false;
    auto result = compile_body_impl(reached_end);

    if (result == body_result::else_) {
      // Section has {{else}}. Emit trampoline jump past else body.
      auto jump_instr = static_cast<std::uint32_t>(bc_.current_offset());
      bc_.add_instruction(bc_opcode::emit_else, 0, 0);

      // Patch operand3 = first else body instruction
      bc_.instructions[section_instr_idx].operand3 =
          static_cast<std::uint32_t>(bc_.current_offset());

      // Compile else body (until close tag)
      bool else_reached_end = false;
      auto else_result = compile_body_impl(else_reached_end);
      if (else_result == body_result::eof) {
        bc_.error = error_ctx{section_instr_idx, error_code::unexpected_end, key};
        return;
      }

      bc_.add_instruction(bc_opcode::emit_end);
      auto body_end = static_cast<std::uint32_t>(bc_.current_offset());
      bc_.patch_jump(section_instr_idx, body_end);
      bc_.patch_jump(jump_instr, body_end);

    } else if (result == body_result::close) {
      // No else — existing path
      bc_.add_instruction(bc_opcode::emit_end);
      bc_.patch_jump(section_instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
    } else {
      bc_.error = error_ctx{section_instr_idx, error_code::unexpected_end, key};
      return;
    }

    if (trim_blocks_ && pos_ < tmpl_.size() && tmpl_[pos_] == '\n') ++pos_;
  }

  enum class skip_result { end, else_, eof };

  /**
   * @brief ブロックタグ本文をバイトコード生成なしでスキップする
   * @details depth 0 で {{else}} または閉じタグ {{/...}} に達するまで pos_ を進める。
   *          ネストしたブロックタグ {{#...}} は depth を増やしてスキップする。
   */
  skip_result skip_to_else_or_end() {
    int depth = 0;
    while (pos_ < tmpl_.size()) {
      auto tag_start = tmpl_.find("{{", pos_);
      if (tag_start == std::string_view::npos) {
        pos_ = tmpl_.size();
        return skip_result::eof;
      }
      pos_ = tag_start;
      if (tag_start + 2 < tmpl_.size() && tmpl_[tag_start + 2] == '{') {
        auto end = tmpl_.find("}}}", tag_start + 3);
        if (end == std::string_view::npos) {
          pos_ = tag_start + 1;
          continue;
        }
        pos_ = end + 3;
        if (trim_blocks_ && pos_ < tmpl_.size() && tmpl_[pos_] == '\n') ++pos_;
        continue;
      }
      auto tag_end = tmpl_.find("}}", tag_start + 2);
      if (tag_end == std::string_view::npos) {
        pos_ = tag_start + 1;
        continue;
      }
      auto inner = trim_sv(tmpl_.substr(tag_start + 2, tag_end - tag_start - 2));
      pos_ = tag_end + 2;
      if (trim_blocks_ && pos_ < tmpl_.size() && tmpl_[pos_] == '\n') ++pos_;
      if (inner.empty()) continue;
      if (inner == "else" && depth == 0) {
        return skip_result::else_;
      }
      if (inner.starts_with("/")) {
        if (depth == 0) return skip_result::end;
        --depth;
        continue;
      }
      if (inner.starts_with("#")) {
        ++depth;
        continue;
      }
    }
    return skip_result::eof;
  }

  /**
   * @brief if 条件分岐をコンパイルする
   * @param expr 条件式の変数名
   * @details {{#if expr}}...{{else}}...{{/if}} の構文を emit_if / emit_else / emit_endif 命令に変換する。
   *          else がある場合とない場合の両方を処理する。
   *          定数条件（if 0 / if 1）はコンパイル時に解決し、到達不可能な分岐のバイトコード生成を省略する。
   */
  void compile_if(std::string_view expr_full) {
    /** 定数条件の最適化: リテラル整数はコンパイル時に真偽判定し、到達不可能な分岐をスキップ */
    if (expr_full.find('|') == std::string_view::npos &&
        expr_full.find("||") == std::string_view::npos &&
        expr_full.find("&&") == std::string_view::npos) {
      auto check_expr = trim_sv(expr_full);
      bool negate = false;
      if (check_expr.starts_with("!")) {
        negate = true;
        check_expr = trim_sv(check_expr.substr(1));
      }
      if (auto int_val = parse_int_literal(check_expr)) {
        bool cond = negate ? (*int_val == 0) : (*int_val != 0);
        if (!cond) {
          auto result = skip_to_else_or_end();
          if (result == skip_result::eof) {
            bc_.error = error_ctx{pos_, error_code::unexpected_end, "if"};
            return;
          }
          if (result == skip_result::else_) {
            bool else_reached_end = false;
            auto else_result = compile_body_impl(else_reached_end);
            if (bc_.error.ec == error_code::none && else_result == body_result::eof) {
              bc_.error = error_ctx{pos_, error_code::unexpected_end, "if"};
            }
          }
        } else {
          bool reached_end = false;
          auto result = compile_body_impl(reached_end);
          if (bc_.error.ec == error_code::none && reached_end) {
            bc_.error = error_ctx{pos_, error_code::unexpected_end, "if"};
            return;
          }
          if (result == body_result::else_) {
            skip_to_else_or_end();
          }
        }
        if (trim_blocks_ && pos_ < tmpl_.size() && tmpl_[pos_] == '\n') ++pos_;
        return;
      }
    }

    auto finish_if = [this](std::size_t if_instr_idx) {
      bool reached_end = false;
      auto result = compile_body_impl(reached_end);

      if (result == body_result::else_) {
        auto else_jump_idx = static_cast<std::uint32_t>(bc_.current_offset());
        bc_.add_instruction(bc_opcode::emit_else, 0, 0);

        bool else_reached_end = false;
        auto else_result = compile_body_impl(else_reached_end);
        if (else_result == body_result::eof) reached_end = true;

        auto endif_addr = static_cast<std::uint32_t>(bc_.current_offset());
        bc_.add_instruction(bc_opcode::emit_endif);

        bc_.patch_jump(if_instr_idx, else_jump_idx + 1);
        bc_.patch_jump(else_jump_idx, endif_addr + 1);
      } else {
        auto endif_addr = static_cast<std::uint32_t>(bc_.current_offset());
        bc_.add_instruction(bc_opcode::emit_endif);
        bc_.patch_jump(if_instr_idx, endif_addr + 1);
      }

      if (bc_.error.ec == error_code::none && reached_end) {
        bc_.error = error_ctx{if_instr_idx, error_code::unexpected_end, "if"};
      }
    };

    auto add_if_var_ref = [this](std::string_view key) {
      auto idx = bc_.add_var_ref(key);
      resolve_ref_indices(idx, key);
      return idx;
    };

    auto expr_trimmed = trim_sv(expr_full);
    auto emit_simple_logic = [&](bc_opcode op, std::string_view lhs, std::string_view rhs = {}) -> bool {
      lhs = trim_sv(lhs);
      rhs = trim_sv(rhs);
      if (lhs.empty()) {
        return false;
      }
      auto lhs_idx = add_if_var_ref(lhs);
      auto rhs_idx = std::uint32_t{0};
      if (op != bc_opcode::emit_if_not) {
        if (rhs.empty()) {
          return false;
        }
        rhs_idx = add_if_var_ref(rhs);
      }
      bc_.add_instruction(op, 0, lhs_idx, rhs_idx);
      finish_if(bc_.current_offset() - 1);
      return true;
    };

    if (auto or_pos = expr_trimmed.find("||"); or_pos != std::string_view::npos) {
      if (emit_simple_logic(bc_opcode::emit_if_or, expr_trimmed.substr(0, or_pos), expr_trimmed.substr(or_pos + 2))) {
        return;
      }
    }
    if (auto and_pos = expr_trimmed.find("&&"); and_pos != std::string_view::npos) {
      if (emit_simple_logic(bc_opcode::emit_if_and, expr_trimmed.substr(0, and_pos), expr_trimmed.substr(and_pos + 2))) {
        return;
      }
    }
    if (expr_trimmed.starts_with("!")) {
      if (emit_simple_logic(bc_opcode::emit_if_not, expr_trimmed.substr(1))) {
        return;
      }
    }

    /** フィルタチェーンの解析 */
    auto parts = split_by_pipe(expr_full);
    auto expr = parts.empty() ? std::string_view{} : parts[0];
    std::vector<string_filter_entry> filters;
    std::vector<int_filter_entry> int_filters;
    std::vector<float_filter_entry> float_filters;
    for (std::size_t fi = 1; fi < parts.size(); ++fi) {
      auto sf = parse_string_filter(parts[fi]);
      if (sf) { filters.push_back(*sf); continue; }
      auto ifl = parse_int_filter(parts[fi]);
      if (ifl) { int_filters.push_back(*ifl); continue; }
      auto ffl = parse_float_filter(parts[fi]);
      if (ffl) { float_filters.push_back(*ffl); continue; }
      bc_.error = error_ctx{pos_, error_code::unknown_filter, parts[fi]};
      return;
    }

    auto idx = bc_.add_var_ref(expr);
    resolve_ref_indices(idx, expr);
    bc_.var_refs[idx].filters = filters;
    bc_.var_refs[idx].int_filters = int_filters;
    bc_.var_refs[idx].float_filters = float_filters;
    stabilize_filter_strings(idx);

    /** {{#if x == N}} / {{#if x != N}} の比較演算子検出 */
    bc_opcode compare_op = bc_opcode::emit_if;
    bool has_filters_local = !filters.empty() || !int_filters.empty() || !float_filters.empty();
    if (has_filters_local) {
      /* フィルタがある場合は比較演算子は使えない */
    } else {
      struct compare_token {
        std::string_view token;
        bc_opcode op;
      };
      auto constexpr compare_tokens = std::array{
        compare_token{"==", bc_opcode::emit_if_eq},
        compare_token{"!=", bc_opcode::emit_if_ne},
        compare_token{">=", bc_opcode::emit_if_gte},
        compare_token{"<=", bc_opcode::emit_if_lte},
        compare_token{">", bc_opcode::emit_if_gt},
        compare_token{"<", bc_opcode::emit_if_lt},
      };

      auto compare_token_it = compare_tokens.end();
      auto op_pos = std::string_view::npos;
      for (auto it = compare_tokens.begin(); it != compare_tokens.end(); ++it) {
        auto const pos = expr.find(it->token);
        if (pos != std::string_view::npos) {
          compare_token_it = it;
          op_pos = pos;
          break;
        }
      }

      if (compare_token_it != compare_tokens.end()) {
        auto lhs = trim_sv(expr.substr(0, op_pos));
        auto rhs = trim_sv(expr.substr(op_pos + compare_token_it->token.size()));
        /** 右辺を整数リテラルとして解釈 */
        auto lit_val = parse_int_literal(rhs);
        if (!lhs.empty()) {
          /** 左辺の変数参照を作り直す */
          auto cmp_idx = bc_.add_var_ref(lhs);
          resolve_ref_indices(cmp_idx, lhs);
          auto cmp_field_idx = bc_.var_refs[cmp_idx].field_index;
          /** rhs は var_ref に保持する */
          auto& cmp_ref = bc_.var_refs[cmp_idx];
          if (lit_val) {
            cmp_ref.compare_rhs_kind = compare_operand_kind::int_literal;
            cmp_ref.int_filters.push_back({int_filter::eq, *lit_val});
          } else if (auto str_lit = parse_string_literal(rhs)) {
            /** 文字列リテラルの場合: LHS フィールド型が enum なら列挙子名→整数に変換 */
            bool resolved_as_enum = false;
            if (cmp_field_idx != UINT32_MAX) {
              /** 現在のコンテキスト型で enum 解決を試み、失敗時はルート型でも試す */
              auto ev = ctx_stack_.back().enum_lookup(cmp_field_idx, *str_lit);
              if (!ev) {
                ev = compile_ctx_enum_lookup<T>(cmp_field_idx, *str_lit);
              }
              if (ev) {
                cmp_ref.compare_rhs_kind = compare_operand_kind::int_literal;
                cmp_ref.int_filters.push_back({int_filter::eq, static_cast<int>(*ev)});
                resolved_as_enum = true;
              }
            }
            if (!resolved_as_enum) {
              /** 通常の文字列リテラル比較として保持 */
              cmp_ref.compare_rhs_kind = compare_operand_kind::string_literal;
              cmp_ref.compare_rhs_text.assign(str_lit->data(), str_lit->size());
            }
          } else if (!rhs.empty()) {
            cmp_ref.compare_rhs_kind = compare_operand_kind::variable;
            cmp_ref.compare_rhs_text.assign(rhs.data(), rhs.size());
            cmp_ref.compare_rhs_field_index = ctx_resolve(rhs);
            cmp_ref.compare_rhs_has_dot = (rhs.find('.') != std::string_view::npos);
          }

          if (cmp_ref.compare_rhs_kind != compare_operand_kind::none) {
            bc_.add_instruction(compare_token_it->op, 0, cmp_idx);
            compare_op = bc_opcode::halt; /* dummy: do not emit emit_if below */
          }
        }
      }
    }

    /** フィルタがある場合は resolve_filtered → filter_* 命令列を発行し、emit_if_filtered を使う */
    bool has_filters_actual = !filters.empty() || !int_filters.empty() || !float_filters.empty();
    if (compare_op == bc_opcode::emit_if) {
      if (has_filters_actual) {
        auto filter_count = static_cast<std::uint32_t>(filters.size() + int_filters.size() + float_filters.size());
        bc_.add_instruction(bc_opcode::resolve_filtered, filter_count, idx);
        emit_filter_chain([this](bc_opcode op, std::uint32_t a = 0, std::uint32_t a2 = 0, std::uint32_t a3 = 0) {
          bc_.add_instruction(op, a, a2, a3);
        }, filters, int_filters, float_filters);
        bc_.add_instruction(bc_opcode::emit_if_filtered, 0, idx);
      } else {
        bc_.add_instruction(bc_opcode::emit_if, 0, idx);
      }
    }

    auto if_instr_idx = bc_.current_offset() - 1;
    finish_if(if_instr_idx);
    if (trim_blocks_ && pos_ < tmpl_.size() && tmpl_[pos_] == '\n') ++pos_;
  }

  /**
   * @brief ループ状態を用いた反転セクション（{{^loop.is_first}}）をコンパイルする
   * @param key loop.index / loop.is_first / loop.is_last のいずれか
   * @details ループ状態の値を用いて真偽判定を行う反転セクションをコンパイルする。
   *          kind フィールドに index=0 / is_first=1 / is_last=2 をエンコードする。
   */
  void compile_at_inverted(std::string_view key) {
    auto k = parse_loop_kind(key);
    if (!k) return;
    /** @brief ループ状態の種類を数値でエンコード（0=index, 1=first, 2=last） */
    std::uint32_t kind;
    switch (*k) {
      case at_var_kind::index: kind = 0; break;
      case at_var_kind::first: kind = 1; break;
      case at_var_kind::last: kind = 2; break;
      default: return;
    }

    bc_.add_instruction(bc_opcode::emit_at_inverted, 0, kind);
    auto instr_idx = bc_.current_offset() - 1;

    bool found_close = compile_body();
    if (bc_.error.ec == error_code::none && !found_close) {
      bc_.error = error_ctx{instr_idx, error_code::unexpected_end, key};
      return;
    }
    if (trim_blocks_ && pos_ < tmpl_.size() && tmpl_[pos_] == '\n') ++pos_;

    bc_.add_instruction(bc_opcode::emit_end);
    bc_.patch_jump(instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
  }

  /**
   * @brief ループ状態を用いたloopセクション（{{#loop.is_first}}）をコンパイルする
   * @param key loop.index / loop.is_first / loop.is_last のいずれか
   * @details ループ状態の値に応じて本体を条件描画するセクションをコンパイルする。
   *          kind フィールドに index=0 / is_first=1 / is_last=2 をエンコードする。
   */
  void compile_at_section(std::string_view key) {
    auto k = parse_loop_kind(key);
    if (!k) return;
    std::uint32_t kind;
    switch (*k) {
      case at_var_kind::index: kind = 0; break;
      case at_var_kind::first: kind = 1; break;
      case at_var_kind::last: kind = 2; break;
      default: return;
    }

    bc_.add_instruction(bc_opcode::emit_at_section, 0, kind);
    auto instr_idx = bc_.current_offset() - 1;

    bool found_close = compile_body();
    if (bc_.error.ec == error_code::none && !found_close) {
      bc_.error = error_ctx{instr_idx, error_code::unexpected_end, key};
      return;
    }
    if (trim_blocks_ && pos_ < tmpl_.size() && tmpl_[pos_] == '\n') ++pos_;

    bc_.add_instruction(bc_opcode::emit_end);
    bc_.patch_jump(instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
  }

  /**
   * @brief テンプレート本体をコンパイルする
   * @param[out] reached_end テンプレート終端に到達した場合に true を設定
   * @return close: {{/xxx}} 検出、else_: {{else}} 検出、eof: 終端に到達
   * @details {{{var}}} の raw プレースホルダ、セクション、if、@変数、フィルタに対応。
   *          呼び出し元は {{else}} を適切に処理する責任を持つ。
   */
  body_result compile_body_impl(bool& reached_end) {
    reached_end = false;
    while (pos_ < tmpl_.size()) {
      auto tag_start = tmpl_.find("{{", pos_);
      if (tag_start == std::string_view::npos) {
        emit_literal(tmpl_.substr(pos_));
        reached_end = true;
        return body_result::eof;
      }

      if (tag_start > pos_) {
        auto literal = tmpl_.substr(pos_, tag_start - pos_);
        if (lstrip_blocks_ && is_block_tag_start(tmpl_, tag_start)) {
          literal = trim_tail_whitespace_for_lstrip(literal);
        }
        if (!literal.empty()) {
          emit_literal(literal);
        }
      }

      if (tag_start + 2 < tmpl_.size() && tmpl_[tag_start + 2] == '{') {
        auto end = tmpl_.find("}}}", tag_start + 3);
        if (end == std::string_view::npos) {
          emit_literal(tmpl_.substr(tag_start, 1));
          pos_ = tag_start + 1;
          continue;
        }
        auto key = trim_sv(tmpl_.substr(tag_start + 3, end - tag_start - 3));
        auto parts = split_by_pipe(key);
        auto actual_key = parts[0];
        std::vector<string_filter_entry> filters;
        std::vector<int_filter_entry> int_filters;
        std::vector<float_filter_entry> float_filters;
        for (std::size_t fi = 1; fi < parts.size(); ++fi) {
          auto sf = parse_string_filter(parts[fi]);
          if (sf) { filters.push_back(*sf); continue; }
          auto ifl = parse_int_filter(parts[fi]);
          if (ifl) { int_filters.push_back(*ifl); continue; }
          auto ffl = parse_float_filter(parts[fi]);
          if (ffl) { float_filters.push_back(*ffl); continue; }
          bc_.error = error_ctx{tag_start, error_code::unknown_filter, parts[fi]};
          return body_result::eof;
        }
        // {{{field.size}}} → emit_var_size (raw)
        if (actual_key.ends_with(".size") && filters.empty() && int_filters.empty() && float_filters.empty()) {
          emit_var_size(actual_key.substr(0, actual_key.size() - 5), true);
        } else if (actual_key.starts_with("root.")) {
          emit_root_field(actual_key, true);
        } else if (auto folded = try_fold_string_constant(actual_key, true, filters, int_filters, float_filters)) {
          emit_literal(*folded);
        } else {
          emit_var(actual_key, true, std::move(filters), std::move(int_filters), std::move(float_filters));
        }
        pos_ = end + 3;
        if (trim_blocks_ && pos_ < tmpl_.size() && tmpl_[pos_] == '\n') ++pos_;
        continue;
      }

      auto tag_end = tmpl_.find("}}", tag_start + 2);
      if (tag_end == std::string_view::npos) {
        emit_literal(tmpl_.substr(tag_start, 1));
        pos_ = tag_start + 1;
        continue;
      }

      auto inner = trim_sv(tmpl_.substr(tag_start + 2, tag_end - tag_start - 2));
      pos_ = tag_end + 2;
      if (trim_blocks_ && pos_ < tmpl_.size() && tmpl_[pos_] == '\n') ++pos_;

      if (inner.empty()) continue;

      if (inner == "/partialdef") {
        continue;
      }
      if (inner.starts_with("/")) {
        return body_result::close;
      }

      if (inner == "else") {
        return body_result::else_;
      }

      if (inner.starts_with("#")) {
        auto key = trim_sv(inner.substr(1));
        if (key == "break") {
          bc_.add_instruction(bc_opcode::emit_break);
          continue;
        }
        if (key == "continue") {
          bc_.add_instruction(bc_opcode::emit_continue);
          continue;
        }
        if (key.starts_with("partialdef ")) {
          continue;
        }
        if (key.starts_with("partiallocal ")) {
          // 内部タグ: local partial の即時呼び出し。local エントリのみ解決する。
          auto partial_name = trim_sv(key.substr(13));
          auto it = std::find_if(bc_.partial_entries.begin(), bc_.partial_entries.end(),
                                  [&](auto const& e) { return e.local && e.name == partial_name; });
          if (it == bc_.partial_entries.end()) {
            bc_.error = error_ctx{tag_start, error_code::unknown_key, partial_name};
            return body_result::eof;
          }
          bc_.add_instruction(bc_opcode::call_partial,
                              static_cast<std::uint32_t>(std::distance(bc_.partial_entries.begin(), it)));
          continue;
        }
        if (key.starts_with("partial ")) {
          auto partial_name = trim_sv(key.substr(8));
          // local partial は名前検索では参照不可
          auto it = std::find_if(bc_.partial_entries.begin(), bc_.partial_entries.end(),
                                  [&](auto const& e) { return !e.local && e.name == partial_name; });
          if (it == bc_.partial_entries.end()) {
            bc_.error = error_ctx{tag_start, error_code::unknown_key, partial_name};
            return body_result::eof;
          }
          bc_.add_instruction(bc_opcode::call_partial,
                              static_cast<std::uint32_t>(std::distance(bc_.partial_entries.begin(), it)));
          continue;
        }
        if (key.starts_with("if") && (key.size() == 2 || key[2] == ' ')) {
          auto expr = key.size() > 2 ? trim_sv(key.substr(3)) : std::string_view{};
          compile_if(expr);
          continue;
        }
        if (parse_loop_kind(key)) {
          compile_at_section(key);
          continue;
        }
        compile_section(key);
        continue;
      }

      if (inner.starts_with(">")) {
        auto partial_name = trim_sv(inner.substr(1));
        auto it = std::find_if(bc_.partial_entries.begin(), bc_.partial_entries.end(),
                                [&](auto const& e) { return e.name == partial_name; });
        if (it == bc_.partial_entries.end()) {
          bc_.error = error_ctx{tag_start, error_code::unknown_key, partial_name};
          return body_result::eof;
        }
        bc_.add_instruction(bc_opcode::call_partial,
                             static_cast<std::uint32_t>(std::distance(bc_.partial_entries.begin(), it)));
        continue;
      }

      if (inner.starts_with("^")) {
        auto key = trim_sv(inner.substr(1));
        if (parse_loop_kind(key)) {
          compile_at_inverted(key);
        } else {
          compile_inverted(key);
        }
        continue;
      }

      if (inner.starts_with("root.")) {
        emit_root_field(inner, false);
        continue;
      }

      if (inner == "this" || inner == ".") {
        bc_.add_instruction(bc_opcode::emit_this);
        continue;
      }

      if (inner == "root") {
        bc_.add_instruction(bc_opcode::emit_at_root);
        continue;
      }

      if (parse_loop_kind(inner)) {
        emit_at_var(inner);
        continue;
      }

      {
        // {{&var}} → 生出力（{{{var}}} と同じ、HTML エスケープなし）
        bool raw_via_ampersand = false;
        auto effective_inner = inner;
        if (!effective_inner.empty() && effective_inner[0] == '&') {
          raw_via_ampersand = true;
          effective_inner = trim_sv(effective_inner.substr(1));
          if (effective_inner.empty()) {
            bc_.error = error_ctx{pos_, error_code::syntax_error, "& の後に変数がありません"};
            return body_result::eof;
          }
        }
        auto parts = split_by_pipe(effective_inner);
        auto key = parts[0];
        std::vector<string_filter_entry> filters;
        std::vector<int_filter_entry> int_filters;
        std::vector<float_filter_entry> float_filters;
        for (std::size_t fi = 1; fi < parts.size(); ++fi) {
          auto sf = parse_string_filter(parts[fi]);
          if (sf) { filters.push_back(*sf); continue; }
          auto ifl = parse_int_filter(parts[fi]);
          if (ifl) { int_filters.push_back(*ifl); continue; }
          auto ffl = parse_float_filter(parts[fi]);
          if (ffl) { float_filters.push_back(*ffl); continue; }
          bc_.error = error_ctx{pos_, error_code::unknown_filter, parts[fi]};
          return body_result::eof;
        }
        // {{field.size}} → emit_var_size
        if (key.ends_with(".size") && filters.empty() && int_filters.empty() && float_filters.empty()) {
          emit_var_size(key.substr(0, key.size() - 5), raw_via_ampersand);
        } else if (auto folded = try_fold_string_constant(key, raw_via_ampersand, filters, int_filters, float_filters)) {
          emit_literal(*folded);
        } else {
          emit_var(key, raw_via_ampersand, std::move(filters), std::move(int_filters), std::move(float_filters));
        }
      }
    }
    reached_end = true;
    return body_result::eof;
  }

  /**
   * @brief テンプレートから {{#partialdef name}}...{{/partialdef}} を抽出し、
   *        各ボディを個別にバイトコードコンパイルして partial_entries に格納する。
   * @param tmpl_str クリーニング済みテンプレート文字列
   * @return partialdef ブロックを除去したメインテンプレート文字列
   */
  std::string extract_partials(std::string const& tmpl_str) {
    std::string_view tmpl = tmpl_str;
    std::string result;
    result.reserve(tmpl.size());

    struct pending_partial {
      std::string name;
      std::string body;
      bool immediate = false;
      bool local = false;
    };
    std::vector<pending_partial> pending;

    std::size_t pos = 0;
    while (pos < tmpl.size()) {
      auto pdef_start = tmpl.find("{{#partialdef", pos);
      if (pdef_start == std::string_view::npos) {
        result.append(tmpl.substr(pos));
        break;
      }

      result.append(tmpl.substr(pos, pdef_start - pos));

      auto tag_end = tmpl.find("}}", pdef_start);
      if (tag_end == std::string_view::npos) {
        result.append(tmpl.substr(pdef_start));
        break;
      }

      auto inner = trim_sv(tmpl.substr(pdef_start + 2, tag_end - pdef_start - 2));
      if (!inner.starts_with("#partialdef ")) {
        result.append(tmpl.substr(pdef_start, tag_end - pdef_start + 2));
        pos = tag_end + 2;
        continue;
      }
      auto name = trim_sv(inner.substr(12));

      // {{#partialdef name [now] [local]}} の修飾子を検出（順不同、併用可）
      //   now   : 定義と同時に即時展開し、後で {{#partial name}} で再利用可能
      //   local : 即時展開のみ。名前検索では参照不可（外部から使えない）
      bool immediate = false;
      bool local = false;
      {
        auto sp = name.find(' ');
        if (sp != std::string_view::npos) {
          auto base = trim_sv(name.substr(0, sp));
          auto rest = name.substr(sp + 1);
          while (!rest.empty()) {
            auto nsp = rest.find(' ');
            auto tok = trim_sv(rest.substr(0, nsp));
            if (tok == "now")
              immediate = true;
            else if (tok == "local") {
              immediate = true;
              local = true;
            }
            rest = (nsp == std::string_view::npos) ? std::string_view{} : rest.substr(nsp + 1);
          }
          name = base;
        }
      }

      auto close_tag = constexpr_find_close_partialdef(tmpl, tag_end + 2);
      if (close_tag == std::string_view::npos) {
        result.append(tmpl.substr(pdef_start));
        break;
      }

      auto body = std::string_view{tmpl}.substr(tag_end + 2, close_tag - (tag_end + 2));
      pending.push_back({std::string(name), std::string(body), immediate, local});

      if (immediate) {
        if (local) {
          // 内部タグ {{#partiallocal <index>}} を残し、即時展開させる。
          // インデックスは登録後の位置なので、ここでは {{#partiallocal name}} とし、
          // ボディパーサが local エントリを直接解決する。
          result += "{{#partiallocal ";
          result += name;
          result += "}}";
        } else {
          // 定義ブロックの代わりに {{#partial name}} 呼び出しタグを残し、即時展開させる
          result += "{{#partial ";
          result += name;
          result += "}}";
        }
      }

      pos = close_tag + 15;
    }

    // 前方参照に対応するため、先に全エントリを名前のみ登録（bc は後で埋める）
    for (auto const& pp : pending) {
      bc_.partial_entries.push_back({pp.name, {}, pp.local});
    }

    // 依存関係を走査し、トポロジカル順を計算
    std::vector<std::size_t> order;
    {
      order.reserve(pending.size());
      std::vector<bool> visited(pending.size(), false);
      std::vector<bool> in_stack(pending.size(), false);  // ponytail: 循環検出用
      auto dfs = [&](auto& self, std::size_t node) -> void {
        if (visited[node]) return;
        visited[node]   = true;
        in_stack[node]  = true;
        std::string_view body = pending[node].body;
        std::size_t pos = 0;
        while (pos < body.size()) {
          auto at = constexpr_find(body, "{{#partial ", pos);
          if (at == std::string_view::npos) break;
          auto end = constexpr_find(body, "}}", at);
          if (end == std::string_view::npos) break;
          std::string_view ref = trim_sv(body.substr(at + 11, end - (at + 11)));
          for (std::size_t j = 0; j < pending.size(); ++j) {
            if (pending[j].name == ref) {
              if (!in_stack[j]) self(self, j);
              break;
            }
          }
          pos = end + 2;
        }
        in_stack[node] = false;
        order.push_back(node);
      };
      for (std::size_t i = 0; i < pending.size(); ++i)
        dfs(dfs, i);
    }

    for (auto idx : order) {
      auto const& pp = pending[idx];
      bc_compiler<T> partial_compiler;
      partial_compiler.set_partial_entries(bc_.partial_entries);
      auto partial_bc = partial_compiler.compile(pp.body, trim_blocks_, lstrip_blocks_);
      if (partial_bc.error.ec != error_code::none) {
        bc_.error = partial_bc.error;
        return {};
      }
      // 同名のプレースホルダを探して bc を埋める（shared_ptr はコピー間で共有）
      auto it = std::find_if(bc_.partial_entries.begin(), bc_.partial_entries.end(),
                             [&](auto const& e) { return e.name == pp.name; });
      it->bc = std::make_shared<bytecode>(std::move(partial_bc));
    }

    // ponytail: ネストした partial 定義を bc_.partial_entries に昇格させる。
    // engine<T>::render_partial(value, "inner") がアクセス可能になる。
    for (std::size_t ei = 0; ei < bc_.partial_entries.size(); ++ei) {
      auto const& entry = bc_.partial_entries[ei];
      if (!entry.bc) continue;
      for (auto const& nested : entry.bc->partial_entries) {
        if (nested.local) continue;
        auto dup = std::find_if(bc_.partial_entries.begin(), bc_.partial_entries.end(),
                                [&](auto const& e) { return e.name == nested.name; });
        if (dup == bc_.partial_entries.end())
          bc_.partial_entries.push_back(nested);
      }
    }

    return result;
  }

  bool compile_body() {
    bool reached_end = false;
    return compile_body_impl(reached_end) != body_result::eof;
  }

 public:
  /**
   * @brief partial エントリを注入して forward reference を可能にする
   *
   * @details 現在の partial_entries を compiler に渡すことで、
   *          後に宣言された partial が以前の partial を参照できるようになる。
   *          nttp_partial_bytecode_holder からも使用される。
   *
   * @param entries 注入する partial エントリのリスト
   */
  void set_partial_entries(std::vector<partial_entry> const& entries) {
    bc_.partial_entries = entries;
  }

  /** @cond private */

  /**
   * @brief テンプレート文字列をバイトコードにコンパイルする
   * @param tmpl Mustache 形式のテンプレート文字列
   * @return コンパイル済みバイトコード
   */
  bytecode compile(std::string_view tmpl, bool trim_blocks = false, bool lstrip_blocks = false) {
    trim_blocks_ = trim_blocks;
    lstrip_blocks_ = lstrip_blocks;
    clean_tmpl_ = transform_exists_sections(strip_bang_comments(strip_comments(strip_standalone_whitespace_tildes(tmpl))));
    bc_.template_storage = clean_tmpl_;
    tmpl_ = bc_.template_storage;
    pos_ = 0;

    // Extract partials before main compilation
    auto main_tmpl = extract_partials(bc_.template_storage);
    if (bc_.error.ec != error_code::none) {
      return std::move(bc_);
    }
    bc_.template_storage = main_tmpl;
    tmpl_ = bc_.template_storage;
    pos_ = 0;

    bool found_close = compile_body();
    if (bc_.error.ec == error_code::none && found_close) {
      bc_.error = error_ctx{pos_, error_code::syntax_error, "stray closing tag"};
      return std::move(bc_);
    }
    bc_.add_instruction(bc_opcode::halt);
    for (auto const& lit : bc_.literals)
      bc_.literal_total_size += lit.size();
    // 単純テンプレ検出をコンパイル時に実施（実行時のオペコード走査を排除）
    bc_.is_simple = true;
    for (auto const& ins : bc_.instructions) {
      if (ins.op != bc_opcode::emit_litvar && ins.op != bc_opcode::emit_litvar_raw
          && ins.op != bc_opcode::emit_literal && ins.op != bc_opcode::halt) {
        bc_.is_simple = false;
        break;
      }
    }
    return std::move(bc_);
  }
};

/**
 * @brief テンプレート文字列をバイトコードにコンパイルする
 * @tparam T コンテキスト型
 * @param tmpl テンプレート文字列
 * @return コンパイル済みバイトコード
 */
template <class T>
bytecode bc_compile(std::string_view tmpl, bool trim_blocks = false, bool lstrip_blocks = false) {
  bc_compiler<T> compiler;
  return compiler.compile(tmpl, trim_blocks, lstrip_blocks);
}

template <class T>
bytecode bc_compile(std::string_view tmpl, std::vector<partial_entry> partials,
                    bool trim_blocks = false, bool lstrip_blocks = false) {
  bc_compiler<T> compiler;
  compiler.set_partial_entries(partials);
  return compiler.compile(tmpl, trim_blocks, lstrip_blocks);
}

template <class T, class ConstMap>
bytecode bc_compile(std::string_view tmpl, ConstMap const& consts, bool trim_blocks = false, bool lstrip_blocks = false) {
  auto expanded = expand_vars_in_template(tmpl, consts);
  if (!expanded) {
    bytecode err_bc;
    err_bc.error = expanded.error();
    return err_bc;
  }
  return bc_compile<T>(*expanded, trim_blocks, lstrip_blocks);
}

} // namespace injamm::detail
