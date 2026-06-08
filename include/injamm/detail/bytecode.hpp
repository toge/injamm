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
  emit_at_root,       /**< @root 出力（ルートコンテキストのシリアライズ） */
  emit_at_root_field,     /**< @root.field ルートフィールド解決（エスケープあり） */
  emit_at_root_field_raw, /**< @root.field ルートフィールド解決（生出力） */
  emit_at_key,        /**< @key 出力（ループ内の現在要素キー名 / インデックス文字列） */
  emit_this,          /**< 現在のコンテキスト自体のシリアライズ */
  resolve_filtered,   /**< フィルタ付き変数解決: 値を一時バッファに解決 */
  filter_upper,       /**< ASCII大文字変換 */
  filter_lower,       /**< ASCII小文字変換 */
  filter_capitalize,  /**< 先頭の文字を大文字にする */
  filter_title,       /**< 単語の先頭を大文字にする */
  filter_trim,        /**< 先頭末尾の空白除去 */
  filter_ltrim,       /**< 先頭の空白除去 */
  filter_rtrim,       /**< 末尾の空白除去 */
  filter_left,        /**< 左寄せ（引数: 幅） */
  filter_right,       /**< 右寄せ（引数: 幅） */
  filter_center,      /**< 中央寄せ（引数: 幅） */
  filter_truncate,    /**< 文字列切り詰め（引数: 最大文字数） */
  filter_substr,      /**< 部分文字列（引数1: 開始位置, 引数2: 文字数） */
  emit_filtered,      /**< フィルタ後の文字列出力（エスケープあり） */
  emit_filtered_raw,  /**< フィルタ後の文字列出力（生出力） */
  filter_int_abs,     /**< 整数絶対値変換 */
  filter_int_hex,     /**< 整数16進数変換 */
  filter_int_oct,     /**< 整数8進数変換 */
  filter_int_bin,     /**< 整数2進数変換 */
  filter_int_neg,     /**< 整数符号逆転 */
  filter_int_mod,     /**< 整数余り（引数: 除数） */
  filter_int_numify,  /**< 整数3桁カンマ区切り */
  halt                /**< プログラム終了 */
};

/**
 * @brief 文字列フィルタの種別
 * @details プレースホルダに適用する文字列変換の種類を定義する
 */
enum class string_filter : std::uint8_t {
  upper,       /**< ASCII小文字→大文字変換 */
  lower,       /**< ASCII大文字→小文字変換 */
  capitalize,  /**< 先頭の文字を大文字にする */
  title,       /**< 単語の先頭を大文字にする */
  trim,        /**< 先頭末尾の空白除去 */
  ltrim,       /**< 先頭の空白除去 */
  rtrim,       /**< 末尾の空白除去 */
  left,        /**< 左寄せ（引数: 幅） */
  right,       /**< 右寄せ（引数: 幅） */
  center,      /**< 中央寄せ（引数: 幅） */
  truncate,    /**< 文字列切り詰め（引数: 最大文字数） */
  substr       /**< 部分文字列（引数1: 開始位置, 引数2: 文字数） */
};

/**
 * @brief 文字列フィルタエントリ（引数付き）
 * @details フィルタの種別と、引数を必要とするフィルタの幅/最大文字数を保持する
 */
struct string_filter_entry {
  string_filter filter; /**< フィルタの種別 */
  int arg1 = 0;         /**< 第1引数（left/right/center/truncate/substr の幅/開始位置） */
  int arg2 = 0;         /**< 第2引数（substr の文字数） */
};

/**
 * @brief 整数変換フィルタの種別
 * @details プレースホルダに適用する整数変換の種類を定義する
 */
enum class int_filter : std::uint8_t {
  abs,     /**< 絶対値 */
  hex,     /**< 16進数表記 */
  oct,     /**< 8進数表記 */
  bin,     /**< 2進数表記 */
  neg,     /**< 符号の逆転 */
  mod,     /**< 余り（引数: 除数） */
  numify   /**< 3桁ごとにカンマ区切り */
};

/**
 * @brief 整数フィルタエントリ（引数付き）
 * @details フィルタの種別と、引数を必要とするフィルタの値を保持する
 */
struct int_filter_entry {
  int_filter filter; /**< フィルタの種別 */
  int arg = 0;       /**< 引数（mod の除数） */
};

/**
 * @brief 変数参照情報
 * @details テンプレート内の変数参照を表す。コンパイル時に glaze リフレクションで
 *          フィールドインデックスが解決可能な場合は field_index に値が設定される。
 */
struct bc_var_ref {
  std::string_view key;                    /**< 変数名 */
  std::uint32_t field_index = UINT32_MAX;  /**< コンパイル時解決済みフィールドインデックス */
  std::vector<string_filter_entry> filters; /**< 文字列フィルタチェーン */
  std::vector<int_filter_entry> int_filters; /**< 整数フィルタチェーン */
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
    var_refs.push_back({key, UINT32_MAX, {}});
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
