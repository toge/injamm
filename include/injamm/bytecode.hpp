#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "types.hpp"

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
  emit_end,           /**< セクション終了 */
  emit_inverted,      /**< 反転セクション開始（^section） */
  emit_at_index,      /**< @index 出力（ループ内の現在インデックス） */
  emit_at_first,      /**< @first 出力（ループの先頭要素か） */
  emit_at_last,       /**< @last 出力（ループの末尾要素か） */
  emit_if,            /**< if 条件分岐 */
  emit_if_eq,         /**< if (var == int_literal) 分岐 */
  emit_if_ne,         /**< if (var != int_literal) 分岐 */
  emit_if_gt,         /**< if (var > int_literal) 分岐 */
  emit_if_gte,        /**< if (var >= int_literal) 分岐 */
  emit_if_lt,         /**< if (var < int_literal) 分岐 */
  emit_if_lte,        /**< if (var <= int_literal) 分岐 */
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
  filter_replace,     /**< 部分文字列置換 */
  filter_default,     /**< デフォルト値フィルタ（引数: literal index） */
  filter_json,        /**< JSON出力フィルタ */
  filter_safe,        /**< 生出力マーク（エスケープ抑制） */
  filter_indent,      /**< インデント（引数: 空白数） */
  filter_pad,         /**< パディング（引数1: 幅, 引数2: 埋め文字literal index） */
  filter_pluralize,   /**< 単数形/複数形（引数1: 単数literal index, 引数2: 複数literal index） */
  filter_format,      /**< strftime 形式 chrono フォーマット（dispatch 時は no-op） */
  emit_filtered,      /**< フィルタ後の文字列出力（エスケープあり） */
  emit_filtered_raw,  /**< フィルタ後の文字列出力（生出力） */
  filter_int_abs,     /**< 整数絶対値変換 */
  filter_int_hex,     /**< 整数16進数変換 */
  filter_int_oct,     /**< 整数8進数変換 */
  filter_int_bin,     /**< 整数2進数変換 */
  filter_int_neg,     /**< 整数符号逆転 */
  filter_int_mod,     /**< 整数余り（引数: 除数） */
  filter_int_numify,        /**< 整数3桁カンマ区切り */
  filter_int_is_neg,           /**< 負数判定: "true"/"false" を出力 */
  filter_int_eq,               /**< 等価判定: 引数と比較し "true"/"false" を出力 */
  filter_int_ne,               /**< 不等価判定: 引数と比較し "true"/"false" を出力 */
  filter_int_gt,               /**< 大なり判定: 引数より大きければ "true"/"false" を出力 */
  filter_int_gte,              /**< 以上判定: 引数以上なら "true"/"false" を出力 */
  filter_int_lt,               /**< 小なり判定: 引数未満なら "true"/"false" を出力 */
  filter_int_lte,              /**< 以下判定: 引数以下なら "true"/"false" を出力 */
  filter_int_zerofill,         /**< 整数0埋め（引数: 最小桁数） */
  filter_int_add,              /**< 整数加算（引数: 加算値） */
  filter_int_sub,              /**< 整数減算（引数: 減算値） */
  filter_int_mul,              /**< 整数乗算（引数: 乗算値） */
  filter_int_div,              /**< 整数除算（引数: 除算値） */
  filter_float_precision,     /**< 実数小数点以下桁数（引数: 桁数） */
  emit_if_filtered,           /**< フィルタ適用済み値での if 分岐 */
  emit_break,         /**< ループ脱出 */
  emit_continue,      /**< 次のイテレーションへスキップ */
  emit_at_index1,     /**< ループ1始まりインデックス ({{@index1}}) */
  emit_at_size,       /**< ループ総要素数 ({{@size}}) */
  emit_var_size,      /**< 変数の要素数 ({{field.size}}) */
  emit_if_or,         /**< if (a || b) 分岐 */
  emit_if_and,        /**< if (a && b) 分岐 */
  emit_if_not,        /**< if (!a) 分岐 */
  call_partial,       /**< 名前付きpartial呼び出し (operand: partial_entry index) */
  halt                /**< プログラム終了 */
};

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
  substr,      /**< 部分文字列（引数1: 開始位置, 引数2: 文字数） */
  replace,     /**< 部分文字列置換 */
  default_value, /**< デフォルト値（引数: フォールバック文字列） */
  to_json,     /**< JSON出力 */
  safe,        /**< 生出力マーク（エスケープ抑制、コンパイル時処理） */
  indent,      /**< 各行にインデント追加（引数: 空白数） */
  pad,         /**< パディング（引数1: 幅, 引数2: 埋め文字） */
  pluralize,   /**< 単数形/複数形（引数1: 単数形, 引数2: 複数形） */
  format       /**< strftime 形式 chrono フォーマット（dispatch 時は no-op） */
};

struct string_filter_entry {
  string_filter filter;       /**< フィルタの種別 */
  int arg1 = 0;               /**< 第1引数（left/right/center/truncate/substr の幅/開始位置） */
  int arg2 = 0;               /**< 第2引数（substr の文字数） */
  std::string_view str_arg1;  /**< 文字列引数1（replace の old 文字列） */
  std::string_view str_arg2;  /**< 文字列引数2（replace の new 文字列） */
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
  numify,  /**< 3桁ごとにカンマ区切り */
  is_neg,  /**< 負数判定（真偽値出力: "true"/"false"） */
  eq,      /**< 等価判定（引数: 比較値、真偽値出力: "true"/"false"） */
  ne,      /**< 不等価判定（引数: 比較値、真偽値出力: "true"/"false"） */
  gt,      /**< 大なり判定（引数: 比較値、真偽値出力: "true"/"false"） */
  gte,     /**< 以上判定（引数: 比較値、真偽値出力: "true"/"false"） */
  lt,      /**< 小なり判定（引数: 比較値、真偽値出力: "true"/"false"） */
  lte,     /**< 以下判定（引数: 比較値、真偽値出力: "true"/"false"） */
  zerofill, /**< 0埋め（引数: 最小桁数） */
  add,      /**< 加算（引数: 加算値） */
  sub,      /**< 減算（引数: 減算値） */
  mul,      /**< 乗算（引数: 乗算値） */
  div       /**< 除算（引数: 除算値） */
};

struct int_filter_entry {
  int_filter filter; /**< フィルタの種別 */
  int arg = 0;       /**< 引数（mod の除数） */
};

/**
 * @brief 実数変換フィルタの種別
 * @details プレースホルダに適用する実数変換の種類を定義する
 */
enum class float_filter : std::uint8_t {
  precision   /**< 小数点以下桁数（引数: 桁数） */
};

struct float_filter_entry {
  float_filter filter; /**< フィルタの種別 */
  int arg = 0;         /**< 引数（precision の小数点以下桁数） */
};

/**
 * @brief if 比較の右オペランド種別
 */
enum class compare_operand_kind : std::uint8_t {
  none,
  int_literal,
  string_literal,
  variable
};

/**
 * @brief 変数参照情報
 * @details テンプレート内の変数参照を表す。コンパイル時に glaze リフレクションで
 *          フィールドインデックスが解決可能な場合は field_index に値が設定される。
 */
struct bc_var_ref {
  std::string key;                         /**< 変数名 */
  std::uint32_t field_index = UINT32_MAX;  /**< コンパイル時解決済みフィールドインデックス */
  bool has_dot = false;                    /**< ドット区切りパス（ネスト）を持つか */
  bool is_loop_parent = false;             /**< コンパイル時解決: key が "loop.parent." 始まりか（ホットパスの文字列比較排除用） */
  compare_operand_kind compare_rhs_kind = compare_operand_kind::none; /**< if 比較の右オペランド種別 */
  std::string compare_rhs_text;            /**< 右オペランド文字列（文字列リテラル or 変数名） */
  std::uint32_t compare_rhs_field_index = UINT32_MAX; /**< 右オペランドの解決済みフィールドインデックス */
  bool compare_rhs_has_dot = false;        /**< 右オペランドがドット区切りパスか */
  /** @brief コンパイル時に事前計算されたフィルタ特殊フラグ（ホットパスのループ排除用）
   *  bit 0: json フィルタ含有
   *  bit 1: chrono format フィルタ含有
   */
  std::uint8_t filter_flags = 0;
  std::vector<string_filter_entry> filters; /**< 文字列フィルタチェーン */
  std::vector<int_filter_entry> int_filters; /**< 整数フィルタチェーン */
  std::vector<float_filter_entry> float_filters; /**< 実数フィルタチェーン */
};

/**
 * @brief 中間命令
 * @details オペコードと最大3つのオペランドからなる中間表現命令。
 *          operand はリテラルインデックスまたはジャンプ先オフセット、
 *          operand2 は変数参照インデックスとして使用される。
 */
struct bc_instruction {
  bc_opcode op;                    /**< オペコード */
  std::uint32_t operand = 0;       /**< リテラルインデックスまたはジャンプ先オフセット */
  std::uint32_t operand2 = 0;      /**< 変数参照インデックス（セクション/inverted/if用） */
  std::uint32_t operand3 = 0;      /**< else_target（セクション/inverted の else 本体開始、0 = なし） */
};

struct bytecode;

/**
 * @brief 名前付きpartialエントリ
 * @details プリコンパイルされたpartial本体のバイトコード。
 *          コンパイル時に各 {{#partialdef name}}...{{/partialdef}} から生成される。
 */
struct partial_entry {
  std::string name;              /**< partial 名 */
  std::shared_ptr<bytecode> bc;  /**< プリコンパイル済みバイトコード */
  bool local = false;            /**< local partial の場合 true（名前検索では参照不可） */
};

/**
 * @brief コンパイル済みバイトコード
 * @details 命令列、リテラルテーブル、変数参照テーブルを保持する。
 *          bc_compiler によって生成され、bc_template によって実行される。
 */
struct bytecode {
  std::vector<bc_instruction> instructions;  /**< 命令列 */
  std::vector<std::string> literals;         /**< リテラル文字列テーブル */
  std::vector<bc_var_ref> var_refs;          /**< 変数参照テーブル */
  std::size_t literal_total_size = 0;        /**< 全リテラルの合計サイズ（出力バッファ事前確保用） */
  error_ctx error{};                         /**< コンパイル時エラー（非ゼロ ec でエラー） */
  std::string template_storage;              /**< テンプレート文字列（string_view の生存期間保証用） */
  std::vector<partial_entry> partial_entries;/**< プリコンパイル済みpartialエントリ（call_partial 用） */
  bool is_simple = false;                   /**< コンパイル時解決: 単純テンプレ（litvar/literal/halt のみ）なら高速パスを使用 */

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
   * @brief 命令を追加する（else_target 付き）
   * @param op オペコード
   * @param operand 第1オペランド
   * @param operand2 第2オペランド
   * @param operand3 第3オペランド（else_target）
   */
  void add_instruction(bc_opcode op, std::uint32_t operand, std::uint32_t operand2, std::uint32_t operand3) {
    instructions.push_back({op, operand, operand2, operand3});
  }

  /**
   * @brief リテラルを追加する
   * @param lit 追加するリテラル文字列
   * @return 追加されたリテラルのインデックス
   */
  std::uint32_t add_literal(std::string_view lit) {
    auto idx = static_cast<std::uint32_t>(literals.size());
    literals.emplace_back(lit);
    return idx;
  }

  /**
   * @brief 変数参照を追加する
   * @param key 変数名
   * @return 追加された変数参照のインデックス
   */
  std::uint32_t add_var_ref(std::string_view key) {
    auto idx = static_cast<std::uint32_t>(var_refs.size());
    bc_var_ref ref;
    ref.key = std::string{key};
    ref.field_index = UINT32_MAX;
    ref.has_dot = (key.find('.') != std::string_view::npos);
    ref.is_loop_parent = key.starts_with("loop.parent.");
    var_refs.push_back(std::move(ref));
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

  /**
   * @brief バイトコードを可読な形式に逆アセンブルする
   * @details デバッグと最適化のために、命令列・リテラルテーブル・変数参照テーブルを
   *          人間に読みやすい形式で出力する。
   * @return std::string 逆アセンブル結果の文字列
   */
  [[nodiscard]] std::string disassemble() const;
};

} // namespace injamm::detail
