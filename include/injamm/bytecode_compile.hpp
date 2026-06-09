#pragma once

#include "bytecode.hpp"
#include "parse.hpp"
#include <string_view>

namespace injamm::detail {

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
  /** @brief テンプレート文字列上の現在位置 */
  std::size_t pos_ = 0;

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
    // フィルタの有無で分岐
    if (filters.empty() && int_filters.empty() && float_filters.empty()) {
      // 既存の高速パス（変更なし）
      if (!bc_.instructions.empty()) {
        auto& last = bc_.instructions.back();
        if (last.op == bc_opcode::emit_literal) {
          last.op = raw ? bc_opcode::emit_litvar_raw : bc_opcode::emit_litvar;
          last.operand2 = idx;
          return;
        }
      }
      bc_.add_instruction(raw ? bc_opcode::emit_var_raw : bc_opcode::emit_var, idx);
    } else {
      // フィルタ専用パス
      bc_.add_instruction(bc_opcode::resolve_filtered, 0, idx);
      for (auto f : filters) {
        switch (f.filter) {
          case string_filter::upper:      bc_.add_instruction(bc_opcode::filter_upper); break;
          case string_filter::lower:      bc_.add_instruction(bc_opcode::filter_lower); break;
          case string_filter::capitalize: bc_.add_instruction(bc_opcode::filter_capitalize); break;
          case string_filter::title:      bc_.add_instruction(bc_opcode::filter_title); break;
          case string_filter::trim:       bc_.add_instruction(bc_opcode::filter_trim); break;
          case string_filter::ltrim:      bc_.add_instruction(bc_opcode::filter_ltrim); break;
          case string_filter::rtrim:      bc_.add_instruction(bc_opcode::filter_rtrim); break;
          case string_filter::left:       bc_.add_instruction(bc_opcode::filter_left, f.arg1); break;
          case string_filter::right:      bc_.add_instruction(bc_opcode::filter_right, f.arg1); break;
          case string_filter::center:     bc_.add_instruction(bc_opcode::filter_center, f.arg1); break;
          case string_filter::truncate:   bc_.add_instruction(bc_opcode::filter_truncate, f.arg1); break;
          case string_filter::substr:     bc_.add_instruction(bc_opcode::filter_substr, f.arg1, f.arg2); break;
        }
      }
      for (auto f : int_filters) {
        switch (f.filter) {
          case int_filter::abs:    bc_.add_instruction(bc_opcode::filter_int_abs); break;
          case int_filter::hex:    bc_.add_instruction(bc_opcode::filter_int_hex); break;
          case int_filter::oct:    bc_.add_instruction(bc_opcode::filter_int_oct); break;
          case int_filter::bin:    bc_.add_instruction(bc_opcode::filter_int_bin); break;
          case int_filter::neg:    bc_.add_instruction(bc_opcode::filter_int_neg); break;
          case int_filter::mod:    bc_.add_instruction(bc_opcode::filter_int_mod, f.arg); break;
          case int_filter::numify: bc_.add_instruction(bc_opcode::filter_int_numify); break;
        }
      }
      for (auto f : float_filters) {
        switch (f.filter) {
          case float_filter::precision: bc_.add_instruction(bc_opcode::filter_float_precision, f.arg); break;
        }
      }
      bc_.add_instruction(raw ? bc_opcode::emit_filtered_raw : bc_opcode::emit_filtered);
    }
  }

  /**
   * @brief @root.field 参照命令を発行する
   * @param key @root.xxx 形式のキー全体
   * @param raw 生出力フラグ
   * @details プレフィックス @root. を除去し、残りのパスを var_ref として登録して
   *          emit_at_root_field 命令を発行する。
   */
  void emit_root_field(std::string_view key, bool raw) {
    auto rest = key.substr(6);
    auto idx = bc_.add_var_ref(rest);
    auto field_idx = resolve_field_index<T>(rest);
    if (field_idx != UINT32_MAX) {
      bc_.set_field_index(idx, field_idx);
    }
    bc_.add_instruction(raw ? bc_opcode::emit_at_root_field_raw : bc_opcode::emit_at_root_field, idx);
  }

  /**
   * @brief @index/@first/@last/@root/@key 参照命令を発行する
   * @param key @index / @first / @last / @root のいずれか
   */
  void emit_at_var(std::string_view key) {
    auto k = parse_at_kind(key);
    switch (k) {
      case chunk_at_var::kind::index:
        bc_.add_instruction(bc_opcode::emit_at_index);
        break;
      case chunk_at_var::kind::first:
        bc_.add_instruction(bc_opcode::emit_at_first);
        break;
      case chunk_at_var::kind::last:
        bc_.add_instruction(bc_opcode::emit_at_last);
        break;
      case chunk_at_var::kind::root:
        bc_.add_instruction(bc_opcode::emit_at_root);
        break;
      case chunk_at_var::kind::key:
        bc_.add_instruction(bc_opcode::emit_at_key);
        break;
      default:
        break;
    }
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

    compile_body();

    bc_.add_instruction(bc_opcode::emit_end);
    bc_.patch_jump(section_instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
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

    compile_body();

    bc_.add_instruction(bc_opcode::emit_end);
    bc_.patch_jump(section_instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
  }

  /**
   * @brief if 条件分岐をコンパイルする
   * @param expr 条件式の変数名
   * @details {{#if expr}}...{{else}}...{{/if}} の構文を emit_if / emit_else / emit_endif 命令に変換する。
   *          else がある場合とない場合の両方を処理する。
   */
  void compile_if(std::string_view expr_full) {
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
    }

    auto idx = bc_.add_var_ref(expr);
    auto field_idx = resolve_field_index<T>(expr);
    if (field_idx != UINT32_MAX) {
      bc_.set_field_index(idx, field_idx);
    }
    bc_.var_refs[idx].filters = filters;
    bc_.var_refs[idx].int_filters = int_filters;
    bc_.var_refs[idx].float_filters = float_filters;

    /** フィルタがある場合は resolve_filtered → filter_* 命令列を発行し、emit_if_filtered を使う */
    bool has_filters = !filters.empty() || !int_filters.empty() || !float_filters.empty();
    if (has_filters) {
      bc_.add_instruction(bc_opcode::resolve_filtered, 0, idx);
      for (auto f : filters) {
        switch (f.filter) {
          case string_filter::upper:      bc_.add_instruction(bc_opcode::filter_upper); break;
          case string_filter::lower:      bc_.add_instruction(bc_opcode::filter_lower); break;
          case string_filter::capitalize: bc_.add_instruction(bc_opcode::filter_capitalize); break;
          case string_filter::title:      bc_.add_instruction(bc_opcode::filter_title); break;
          case string_filter::trim:       bc_.add_instruction(bc_opcode::filter_trim); break;
          case string_filter::ltrim:      bc_.add_instruction(bc_opcode::filter_ltrim); break;
          case string_filter::rtrim:      bc_.add_instruction(bc_opcode::filter_rtrim); break;
          case string_filter::left:       bc_.add_instruction(bc_opcode::filter_left, f.arg1); break;
          case string_filter::right:      bc_.add_instruction(bc_opcode::filter_right, f.arg1); break;
          case string_filter::center:     bc_.add_instruction(bc_opcode::filter_center, f.arg1); break;
          case string_filter::truncate:   bc_.add_instruction(bc_opcode::filter_truncate, f.arg1); break;
          case string_filter::substr:     bc_.add_instruction(bc_opcode::filter_substr, f.arg1, f.arg2); break;
        }
      }
      for (auto f : int_filters) {
        switch (f.filter) {
          case int_filter::abs:    bc_.add_instruction(bc_opcode::filter_int_abs); break;
          case int_filter::hex:    bc_.add_instruction(bc_opcode::filter_int_hex); break;
          case int_filter::oct:    bc_.add_instruction(bc_opcode::filter_int_oct); break;
          case int_filter::bin:    bc_.add_instruction(bc_opcode::filter_int_bin); break;
          case int_filter::neg:    bc_.add_instruction(bc_opcode::filter_int_neg); break;
          case int_filter::mod:    bc_.add_instruction(bc_opcode::filter_int_mod, f.arg); break;
          case int_filter::numify: bc_.add_instruction(bc_opcode::filter_int_numify); break;
          case int_filter::is_neg: bc_.add_instruction(bc_opcode::filter_int_is_neg); break;
          case int_filter::eq:     bc_.add_instruction(bc_opcode::filter_int_eq, f.arg); break;
        }
      }
      for (auto f : float_filters) {
        switch (f.filter) {
          case float_filter::precision: bc_.add_instruction(bc_opcode::filter_float_precision, f.arg); break;
        }
      }
      bc_.add_instruction(bc_opcode::emit_if_filtered, 0, idx);
    } else {
      bc_.add_instruction(bc_opcode::emit_if, 0, idx);
    }

    auto if_instr_idx = bc_.current_offset() - 1;

    std::uint32_t else_instr_idx = 0;
    bool has_else = compile_body_with_else(else_instr_idx);

    if (has_else) {
      bc_.add_instruction(bc_opcode::emit_endif);
      bc_.patch_jump(if_instr_idx, else_instr_idx + 1);
      bc_.patch_jump(else_instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
    } else {
      bc_.add_instruction(bc_opcode::emit_endif);
      bc_.patch_jump(if_instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
    }
  }

  /**
   * @brief ループ状態を用いた反転セクション（{{^@var}}）をコンパイルする
   * @param key @index / @first / @last のいずれか
   * @details ループ状態の値を用いて真偽判定を行う反転セクションをコンパイルする。
   *          kind フィールドに @index=0 / @first=1 / @last=2 をエンコードする。
   */
  void compile_at_inverted(std::string_view key) {
    auto k = parse_at_kind(key);
    /** @brief ループ状態の種類を数値でエンコード（0=index, 1=first, 2=last） */
    std::uint32_t kind;
    switch (k) {
      case chunk_at_var::kind::index: kind = 0; break;
      case chunk_at_var::kind::first: kind = 1; break;
      case chunk_at_var::kind::last: kind = 2; break;
      default: return;
    }

    bc_.add_instruction(bc_opcode::emit_at_inverted, 0, kind);
    auto instr_idx = bc_.current_offset() - 1;

    compile_body();

    bc_.add_instruction(bc_opcode::emit_end);
    bc_.patch_jump(instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
  }

  /**
   * @brief ループ状態を用いた@varセクション（{{#@var}}）をコンパイルする
   * @param key @index / @first / @last のいずれか
   * @details ループ状態の値に応じて本体を条件描画するセクションをコンパイルする。
   *          kind フィールドに @index=0 / @first=1 / @last=2 をエンコードする。
   */
  void compile_at_section(std::string_view key) {
    auto k = parse_at_kind(key);
    std::uint32_t kind;
    switch (k) {
      case chunk_at_var::kind::index: kind = 0; break;
      case chunk_at_var::kind::first: kind = 1; break;
      case chunk_at_var::kind::last: kind = 2; break;
      default: return;
    }

    bc_.add_instruction(bc_opcode::emit_at_section, 0, kind);
    auto instr_idx = bc_.current_offset() - 1;

    compile_body();

    bc_.add_instruction(bc_opcode::emit_end);
    bc_.patch_jump(instr_idx, static_cast<std::uint32_t>(bc_.current_offset()));
  }

  /**
   * @brief テンプレート本体をコンパイルする
   * @details テンプレート文字列を先頭から走査し、リテラル・変数・セクション・if などを
   *          バイトコード命令に変換する。{{/xxx}} や {{else}} に遭遇すると戻る。
   *          以下の構文を処理する:
   *          - {{{var}}} : 生出力変数
   *          - {{var}}   : エスケープ付き変数
   *          - {{#section}} : セクション開始
   *          - {{#if expr}} : 条件分岐
   *          - {{^section}} : 反転セクション
   *          - {{#@var}} / {{^@var}} : @var 条件セクション
   *          - {{@index}} / {{@first}} / {{@last}} : ループ変数
   *          - {{/xxx}}  : セクション終了（return）
   *          - {{else}}  : else 節（return）
   */
  void compile_body() {
    while (pos_ < tmpl_.size()) {
      /** @brief 次の {{ タグを検索 */
      auto tag_start = tmpl_.find("{{", pos_);
      if (tag_start == std::string_view::npos) {
        emit_literal(tmpl_.substr(pos_));
        break;
      }

      /** @brief タグ前のリテラルを出力 */
      if (tag_start > pos_) {
        emit_literal(tmpl_.substr(pos_, tag_start - pos_));
      }

      /** @brief {{{ raw プレースホルダの検出 */
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
          if (sf) {
            filters.push_back(*sf);
            continue;
          }
          auto ifl = parse_int_filter(parts[fi]);
          if (ifl) {
            int_filters.push_back(*ifl);
            continue;
          }
          auto ffl = parse_float_filter(parts[fi]);
          if (ffl) {
            float_filters.push_back(*ffl);
            continue;
          }
        }
        if (actual_key.starts_with("@root.")) {
          emit_root_field(actual_key, true);
        } else {
          emit_var(actual_key, true, std::move(filters), std::move(int_filters), std::move(float_filters));
        }
        pos_ = end + 3;
        continue;
      }

      /** @brief }} 終端を検索 */
      auto tag_end = tmpl_.find("}}", tag_start + 2);
      if (tag_end == std::string_view::npos) {
        emit_literal(tmpl_.substr(tag_start, 1));
        pos_ = tag_start + 1;
        continue;
      }

      auto inner = trim_sv(tmpl_.substr(tag_start + 2, tag_end - tag_start - 2));
      pos_ = tag_end + 2;

      if (inner.empty()) continue;

      /** @brief {{/xxx}} 終了タグ */
      if (inner.starts_with("/")) {
        return;
      }

      /** @brief {{else}} */
      if (inner == "else") {
        bc_.add_instruction(bc_opcode::emit_else);
        return;
      }

      /** @brief {{#section}} または {{#if}} */
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

        if (key.starts_with("if") && (key.size() == 2 || key[2] == ' ')) {
          auto expr = key.size() > 2 ? trim_sv(key.substr(3)) : std::string_view{};
          compile_if(expr);
          continue;
        }

        if (key.starts_with("@")) {
          compile_at_section(key);
          continue;
        }

        compile_section(key);
        continue;
      }

      /** @brief {{^section}} または {{^@var}} */
      if (inner.starts_with("^")) {
        auto key = trim_sv(inner.substr(1));
        if (key.starts_with("@")) {
          compile_at_inverted(key);
        } else {
          compile_inverted(key);
        }
        continue;
      }

      /** @brief {{@root.field}} — ルートコンテキストフィールド参照 */
      if (inner.starts_with("@root.")) {
        emit_root_field(inner, false);
        continue;
      }

      /** @brief {{this}} — 現在のコンテキスト自体の参照 */
      if (inner == "this") {
        bc_.add_instruction(bc_opcode::emit_this);
        continue;
      }

      /** @brief {{@index}} / {{@first}} / {{@last}} / {{@root}} */
      if (inner.starts_with("@")) {
        emit_at_var(inner);
        continue;
      }

      /** @brief {{var}} 通常変数（フィルタ対応） */
      {
        auto parts = split_by_pipe(inner);
        auto key = parts[0];
        std::vector<string_filter_entry> filters;
        std::vector<int_filter_entry> int_filters;
        std::vector<float_filter_entry> float_filters;
        for (std::size_t fi = 1; fi < parts.size(); ++fi) {
          auto sf = parse_string_filter(parts[fi]);
          if (sf) {
            filters.push_back(*sf);
            continue;
          }
          auto ifl = parse_int_filter(parts[fi]);
          if (ifl) {
            int_filters.push_back(*ifl);
            continue;
          }
          auto ffl = parse_float_filter(parts[fi]);
          if (ffl) {
            float_filters.push_back(*ffl);
            continue;
          }
        }
        emit_var(key, false, std::move(filters), std::move(int_filters), std::move(float_filters));
      }
    }
  }

  /**
   * @brief else 節の有無を考慮して本体をコンパイルする
   * @param[out] else_instr_idx else 命令のインデックス（else がある場合に設定）
   * @return else がある場合は true、ない場合は false
   * @details compile_body とほぼ同じロジックだが、{{else}} を検出した場合に
   *          else_instr_idx を設定して true を返す。入れ子のセクション/if も適切に処理する。
   */
  bool compile_body_with_else(std::uint32_t& else_instr_idx) {
    while (pos_ < tmpl_.size()) {
      auto tag_start = tmpl_.find("{{", pos_);
      if (tag_start == std::string_view::npos) {
        emit_literal(tmpl_.substr(pos_));
        break;
      }

      if (tag_start > pos_) {
        emit_literal(tmpl_.substr(pos_, tag_start - pos_));
      }

      auto tag_end = tmpl_.find("}}", tag_start + 2);
      if (tag_end == std::string_view::npos) {
        emit_literal(tmpl_.substr(tag_start, 1));
        pos_ = tag_start + 1;
        continue;
      }

      auto inner = trim_sv(tmpl_.substr(tag_start + 2, tag_end - tag_start - 2));
      pos_ = tag_end + 2;

      if (inner.empty()) continue;

      /** @brief {{/xxx}} — else なしでブロック終了 */
      if (inner.starts_with("/")) {
        return false;
      }

      /** @brief {{else}} を検出 — else 節の開始位置を記録 */
      if (inner == "else") {
        else_instr_idx = static_cast<std::uint32_t>(bc_.current_offset());
        bc_.add_instruction(bc_opcode::emit_else, 0, 0);
        compile_body();
        return true;
      }

      /** @brief 入れ子の {{#if}} / {{#section}} */
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
        if (key.starts_with("if") && (key.size() == 2 || key[2] == ' ')) {
          auto expr = key.size() > 2 ? trim_sv(key.substr(3)) : std::string_view{};
          compile_if(expr);
          continue;
        }
        if (key.starts_with("@")) {
          compile_at_section(key);
          continue;
        }
        compile_section(key);
        continue;
      }

      /** @brief 入れ子の {{^section}} / {{^@var}} */
      if (inner.starts_with("^")) {
        auto key = trim_sv(inner.substr(1));
        if (key.starts_with("@")) {
          compile_at_inverted(key);
        } else {
          compile_inverted(key);
        }
        continue;
      }

      /** @brief {{@root.field}} — ルートコンテキストフィールド参照 */
      if (inner.starts_with("@root.")) {
        emit_root_field(inner, false);
        continue;
      }

      /** @brief {{this}} — 現在のコンテキスト自体の参照 */
      if (inner == "this") {
        bc_.add_instruction(bc_opcode::emit_this);
        continue;
      }

      /** @brief {{@index}} / {{@first}} / {{@last}} / {{@root}} */
      if (inner.starts_with("@")) {
        emit_at_var(inner);
        continue;
      }

      /** @brief {{var}} 通常変数（フィルタ対応） */
      {
        auto parts = split_by_pipe(inner);
        auto key = parts[0];
        std::vector<string_filter_entry> filters;
        std::vector<int_filter_entry> int_filters;
        std::vector<float_filter_entry> float_filters;
        for (std::size_t fi = 1; fi < parts.size(); ++fi) {
          auto sf = parse_string_filter(parts[fi]);
          if (sf) {
            filters.push_back(*sf);
            continue;
          }
          auto ifl = parse_int_filter(parts[fi]);
          if (ifl) {
            int_filters.push_back(*ifl);
            continue;
          }
          auto ffl = parse_float_filter(parts[fi]);
          if (ffl) {
            float_filters.push_back(*ffl);
            continue;
          }
        }
        emit_var(key, false, std::move(filters), std::move(int_filters), std::move(float_filters));
      }
    }
    return false;
  }

 public:
  /**
   * @brief テンプレート文字列をバイトコードにコンパイルする
   * @param tmpl Mustache 形式のテンプレート文字列
   * @return コンパイル済みバイトコード
   */
  bytecode compile(std::string_view tmpl) {
    tmpl_ = tmpl;
    pos_ = 0;
    compile_body();
    bc_.add_instruction(bc_opcode::halt);
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
bytecode bc_compile(std::string_view tmpl) {
  bc_compiler<T> compiler;
  return compiler.compile(tmpl);
}

} // namespace injamm::detail
