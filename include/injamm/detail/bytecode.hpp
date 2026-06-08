#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace injamm::detail {

/**
 * @brief バイトコードオペコード
 * @details VM が解釈する命令の種類を定義する。各オペコードは1バイトで表現される。
 */
enum class bc_opcode : std::uint8_t {
  emit_literal,       /**< リテラル文字列出力 */
  emit_var,           /**< 変数参照（HTMLエスケープあり） */
  emit_var_raw,       /**< 変数参照（生出力、エスケープなし） */
  emit_section,       /**< セクション開始（配列ループ） */
  emit_section_bool,  /**< セクション開始（真偽値） */
  emit_end,           /**< セクション終了 */
  emit_inverted,      /**< 反転セクション開始（^section） */
  emit_at_index,      /**< @index 出力（ループ内の現在インデックス） */
  emit_at_first,      /**< @first 出力（ループの先頭要素か） */
  emit_at_last,       /**< @last 出力（ループの末尾要素か） */
  emit_if,            /**< if 条件分岐 */
  emit_else,          /**< else ジャンプ先 */
  emit_endif,         /**< endif（ifブロック終了） */
  emit_at_section,    /**< @first/@last/@index によるセクション制御 */
  emit_at_inverted,   /**< ^@first/^@last/^@index 反転セクション */
  emit_litvar,        /**< 融合命令: リテラル + 変数（エスケープあり） */
  emit_litvar_raw,    /**< 融合命令: リテラル + 変数（生出力） */
  halt                /**< プログラム終了 */
};

/**
 * @brief 変数参照情報
 * @details テンプレート内の変数参照を表す。コンパイル時に glaze リフレクションで
 *          フィールドインデックスが解決可能な場合は field_index に値が設定される。
 */
struct bc_var_ref {
  std::string_view key;                    /**< 変数名 */
  std::uint32_t field_index = UINT32_MAX;  /**< コンパイル時解決済みフィールドインデックス（未解決時はUINT32_MAX） */
};

/**
 * @brief 中間命令
 * @details オペコードと最大2つのオペランドからなる中間表現命令。
 *          operand はリテラルインデックスまたはジャンプ先オフセット、
 *          operand2 は変数参照インデックスとして使用される。
 */
struct bc_instruction {
  bc_opcode op;                    /**< オペコード */
  std::uint32_t operand = 0;       /**< リテラルインデックスまたはジャンプ先オフセット */
  std::uint32_t operand2 = 0;      /**< 変数参照インデックス（セクション/inverted/if用） */
};

/**
 * @brief コンパイル済みバイトコード
 * @details 命令列、リテラルテーブル、変数参照テーブルを保持する。
 *          bc_compiler によって生成され、bc_template によって実行される。
 */
struct bytecode {
  std::vector<bc_instruction> instructions;  /**< 命令列 */
  std::vector<std::string_view> literals;    /**< リテラル文字列テーブル */
  std::vector<bc_var_ref> var_refs;          /**< 変数参照テーブル */

  /**
   * @brief 命令を追加する
   * @param op オペコード
   * @param operand 第1オペランド（デフォルト0）
   * @param operand2 第2オペランド（デフォルト0）
   */
  void add_instruction(bc_opcode op, std::uint32_t operand = 0, std::uint32_t operand2 = 0) {
    instructions.push_back({op, operand, operand2});
  }

  /**
   * @brief リテラルを追加する
   * @param lit 追加するリテラル文字列
   * @return 追加されたリテラルのインデックス
   */
  std::uint32_t add_literal(std::string_view lit) {
    auto idx = static_cast<std::uint32_t>(literals.size());
    literals.push_back(lit);
    return idx;
  }

  /**
   * @brief 変数参照を追加する
   * @param key 変数名
   * @return 追加された変数参照のインデックス
   */
  std::uint32_t add_var_ref(std::string_view key) {
    auto idx = static_cast<std::uint32_t>(var_refs.size());
    var_refs.push_back({key, UINT32_MAX});
    return idx;
  }

  /**
   * @brief 変数参照にフィールドインデックスを設定する
   * @param var_ref_idx 設定対象の変数参照インデックス
   * @param field_index glaze リフレクションで解決したフィールドインデックス
   */
  void set_field_index(std::uint32_t var_ref_idx, std::uint32_t field_index) {
    var_refs[var_ref_idx].field_index = field_index;
  }

  /**
   * @brief 現在の命令数を取得する
   * @return 命令列のサイズ
   */
  std::size_t current_offset() const { return instructions.size(); }

  /**
   * @brief 指定した命令のジャンプ先オペランドを書き換える
   * @param instr_idx 書き換え対象の命令インデックス
   * @param target ジャンプ先オフセット
   */
  void patch_jump(std::size_t instr_idx, std::uint32_t target) {
    instructions[instr_idx].operand = target;
  }
};

} // namespace injamm::detail
