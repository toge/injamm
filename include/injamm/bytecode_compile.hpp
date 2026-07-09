#pragma once

#include <string_view>
#include <type_traits>

#include "bytecode.hpp"
#include "enum_io.hpp"
#include "parse.hpp"
// enum_io.hpp provides enum_name_to_int and serialize_enum

namespace injamm::detail {

template <class Emitter>
void emit_filter_chain(Emitter&& emit, std::vector<string_filter_entry> const& filters,
                       std::vector<int_filter_entry> const& int_filters,
                       std::vector<float_filter_entry> const& float_filters) {
  for (auto f : filters) {
    switch (f.filter) {
      case string_filter::upper:      emit(bc_opcode::filter_upper); break;
      case string_filter::lower:      emit(bc_opcode::filter_lower); break;
      case string_filter::capitalize: emit(bc_opcode::filter_capitalize); break;
      case string_filter::title:      emit(bc_opcode::filter_title); break;
      case string_filter::trim:       emit(bc_opcode::filter_trim); break;
      case string_filter::ltrim:      emit(bc_opcode::filter_ltrim); break;
      case string_filter::rtrim:      emit(bc_opcode::filter_rtrim); break;
      case string_filter::left:       emit(bc_opcode::filter_left, f.arg1); break;
      case string_filter::right:      emit(bc_opcode::filter_right, f.arg1); break;
      case string_filter::center:     emit(bc_opcode::filter_center, f.arg1); break;
      case string_filter::truncate:   emit(bc_opcode::filter_truncate, f.arg1); break;
      case string_filter::substr:     emit(bc_opcode::filter_substr, f.arg1, f.arg2); break;
      case string_filter::replace:       emit(bc_opcode::filter_replace); break;
      case string_filter::default_value: emit(bc_opcode::filter_default, static_cast<std::uint32_t>(0)); break;
      case string_filter::to_json:       emit(bc_opcode::filter_json); break;
      case string_filter::safe:          emit(bc_opcode::filter_safe); break;
      case string_filter::indent:        emit(bc_opcode::filter_indent, static_cast<std::uint32_t>(f.arg1)); break;
      case string_filter::pad:
        emit(bc_opcode::filter_pad, static_cast<std::uint32_t>(f.arg1), UINT32_MAX);
        break;
      case string_filter::pluralize:     emit(bc_opcode::filter_pluralize); break;
      case string_filter::format:        emit(bc_opcode::filter_format); break;
    }
  }
  for (auto f : int_filters) {
    switch (f.filter) {
      case int_filter::abs:    emit(bc_opcode::filter_int_abs); break;
      case int_filter::hex:    emit(bc_opcode::filter_int_hex); break;
      case int_filter::oct:    emit(bc_opcode::filter_int_oct); break;
      case int_filter::bin:    emit(bc_opcode::filter_int_bin); break;
      case int_filter::neg:    emit(bc_opcode::filter_int_neg); break;
      case int_filter::mod:    emit(bc_opcode::filter_int_mod, f.arg); break;
      case int_filter::numify: emit(bc_opcode::filter_int_numify); break;
      case int_filter::is_neg: emit(bc_opcode::filter_int_is_neg); break;
      case int_filter::eq:     emit(bc_opcode::filter_int_eq, f.arg); break;
      case int_filter::ne:     emit(bc_opcode::filter_int_ne, f.arg); break;
      case int_filter::gt:     emit(bc_opcode::filter_int_gt, f.arg); break;
      case int_filter::gte:    emit(bc_opcode::filter_int_gte, f.arg); break;
      case int_filter::lt:     emit(bc_opcode::filter_int_lt, f.arg); break;
      case int_filter::lte:    emit(bc_opcode::filter_int_lte, f.arg); break;
      case int_filter::zerofill: emit(bc_opcode::filter_int_zerofill, f.arg); break;
      case int_filter::add:      emit(bc_opcode::filter_int_add, f.arg); break;
      case int_filter::sub:      emit(bc_opcode::filter_int_sub, f.arg); break;
      case int_filter::mul:      emit(bc_opcode::filter_int_mul, f.arg); break;
      case int_filter::div:      emit(bc_opcode::filter_int_div, f.arg); break;
    }
  }
  for (auto f : float_filters) {
    switch (f.filter) {
      case float_filter::precision: emit(bc_opcode::filter_float_precision, f.arg); break;
    }
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
    auto field_idx = resolve_field_index<T>(key);
    if (field_idx != UINT32_MAX) {
      bc_.set_field_index(idx, field_idx);
    }
    bc_.var_refs[idx].filters = filters;
    bc_.var_refs[idx].int_filters = int_filters;
    bc_.var_refs[idx].float_filters = float_filters;
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
      emit_filter_chain([this](bc_opcode op, std::uint32_t a = 0, std::uint32_t a2 = 0) {
        bc_.add_instruction(op, a, a2);
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
    auto field_idx = resolve_field_index<T>(key);
    if (field_idx != UINT32_MAX) {
      bc_.set_field_index(idx, field_idx);
    }
    bc_.add_instruction(bc_opcode::emit_var_size, idx);
  }

  /**
   * @brief セクション（配列ループ）をコンパイルする
   * @param key セクション変数名
   * @details {{#section}}...{{/section}} の構文を emit_section / emit_end 命令に変換する。
   *          セクション命令には /section の次の命令位置がジャンプ先として書き込まれる。
   */
  void compile_section(std::string_view key) {
    auto idx = bc_.add_var_ref(key);
    auto field_idx = resolve_field_index<T>(key);
    if (field_idx != UINT32_MAX) {
      bc_.set_field_index(idx, field_idx);
    }
    bc_.add_instruction(bc_opcode::emit_section, 0, idx);

    /** @brief 後でジャンプ先を書き込むための命令位置を記録 */
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

  /**
   * @brief 反転セクション（^section）をコンパイルする
   * @param key セクション変数名
   * @details {{^section}}...{{/section}} は変数が偽/空の場合に本体が描画される。
   */
  void compile_inverted(std::string_view key) {
    auto idx = bc_.add_var_ref(key);
    auto field_idx = resolve_field_index<T>(key);
    if (field_idx != UINT32_MAX) {
      bc_.set_field_index(idx, field_idx);
    }
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
      auto field_idx = resolve_field_index<T>(key);
      if (field_idx != UINT32_MAX) {
        bc_.set_field_index(idx, field_idx);
      }
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
    auto field_idx = resolve_field_index<T>(expr);
    if (field_idx != UINT32_MAX) {
      bc_.set_field_index(idx, field_idx);
    }
    bc_.var_refs[idx].filters = filters;
    bc_.var_refs[idx].int_filters = int_filters;
    bc_.var_refs[idx].float_filters = float_filters;

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
          auto cmp_field_idx = resolve_field_index<T>(lhs);
          if (cmp_field_idx != UINT32_MAX) {
            bc_.set_field_index(cmp_idx, cmp_field_idx);
          }
          /** rhs は var_ref に保持する */
          auto& cmp_ref = bc_.var_refs[cmp_idx];
          if (lit_val) {
            cmp_ref.compare_rhs_kind = compare_operand_kind::int_literal;
            cmp_ref.int_filters.push_back({int_filter::eq, *lit_val});
          } else if (auto str_lit = parse_string_literal(rhs)) {
            /** 文字列リテラルの場合: LHS フィールド型が enum なら列挙子名→整数に変換 */
            bool resolved_as_enum = false;
            if (cmp_field_idx != UINT32_MAX) {
              with_field_type_at<T>(cmp_field_idx, [&]<class FT>() {
                if constexpr (std::is_enum_v<FT>) {
                  auto ev = enum_name_to_int<FT>(*str_lit);
                  if (ev) {
                    cmp_ref.compare_rhs_kind = compare_operand_kind::int_literal;
                    cmp_ref.int_filters.push_back({int_filter::eq, static_cast<int>(*ev)});
                    resolved_as_enum = true;
                  }
                }
              });
            }
            if (!resolved_as_enum) {
              /** 通常の文字列リテラル比較として保持 */
              cmp_ref.compare_rhs_kind = compare_operand_kind::string_literal;
              cmp_ref.compare_rhs_text.assign(str_lit->data(), str_lit->size());
            }
          } else if (!rhs.empty()) {
            cmp_ref.compare_rhs_kind = compare_operand_kind::variable;
            cmp_ref.compare_rhs_text.assign(rhs.data(), rhs.size());
            cmp_ref.compare_rhs_field_index = resolve_field_index<T>(rhs);
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
        emit_filter_chain([this](bc_opcode op, std::uint32_t a = 0, std::uint32_t a2 = 0) {
          bc_.add_instruction(op, a, a2);
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
        if (actual_key.starts_with("root.")) {
          emit_root_field(actual_key, true);
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
        if (key.starts_with("partial ")) {
          auto partial_name = trim_sv(key.substr(8));
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

      if (inner == "this") {
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
        auto parts = split_by_pipe(inner);
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
          emit_var_size(key.substr(0, key.size() - 5), false);
        } else {
          emit_var(key, false, std::move(filters), std::move(int_filters), std::move(float_filters));
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

      auto close_tag = tmpl.find("{{/partialdef}}", tag_end + 2);
      if (close_tag == std::string_view::npos) {
        result.append(tmpl.substr(pdef_start));
        break;
      }

      auto body = std::string_view{tmpl}.substr(tag_end + 2, close_tag - (tag_end + 2));
      pending.push_back({std::string(trim_sv(name)), std::string(body)});

      pos = close_tag + 15;
    }

    for (auto& pp : pending) {
      bc_compiler<T> partial_compiler;
      partial_compiler.set_partial_entries(bc_.partial_entries);
      auto partial_bc = partial_compiler.compile(pp.body, trim_blocks_, lstrip_blocks_);
      if (partial_bc.error.ec != error_code::none) {
        bc_.error = partial_bc.error;
        return {};
      }

      partial_entry entry;
      entry.name = pp.name;
      entry.bc = std::make_shared<bytecode>(std::move(partial_bc));
      bc_.partial_entries.push_back(std::move(entry));
    }

    return result;
  }

  bool compile_body() {
    bool reached_end = false;
    return compile_body_impl(reached_end) != body_result::eof;
  }

  void set_partial_entries(std::vector<partial_entry> const& entries) {
    bc_.partial_entries = entries;
  }

 public:
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
