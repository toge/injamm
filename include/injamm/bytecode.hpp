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

[[nodiscard]] inline std::string_view opcode_name(bc_opcode op) noexcept {
  switch (op) {
  case bc_opcode::emit_literal:            return "emit_literal";
  case bc_opcode::emit_var:                return "emit_var";
  case bc_opcode::emit_var_raw:            return "emit_var_raw";
  case bc_opcode::emit_section:                return "emit_section";
  case bc_opcode::emit_end:                    return "emit_end";
  case bc_opcode::emit_inverted:           return "emit_inverted";
  case bc_opcode::emit_at_index:           return "emit_at_index";
  case bc_opcode::emit_at_first:           return "emit_at_first";
  case bc_opcode::emit_at_last:            return "emit_at_last";
  case bc_opcode::emit_if:                 return "emit_if";
  case bc_opcode::emit_if_eq:              return "emit_if_eq";
  case bc_opcode::emit_if_ne:              return "emit_if_ne";
  case bc_opcode::emit_if_gt:              return "emit_if_gt";
  case bc_opcode::emit_if_gte:             return "emit_if_gte";
  case bc_opcode::emit_if_lt:              return "emit_if_lt";
  case bc_opcode::emit_if_lte:             return "emit_if_lte";
  case bc_opcode::emit_else:               return "emit_else";
  case bc_opcode::emit_endif:              return "emit_endif";
  case bc_opcode::emit_at_section:         return "emit_at_section";
  case bc_opcode::emit_at_inverted:        return "emit_at_inverted";
  case bc_opcode::emit_litvar:             return "emit_litvar";
  case bc_opcode::emit_litvar_raw:         return "emit_litvar_raw";
  case bc_opcode::emit_at_root:            return "emit_at_root";
  case bc_opcode::emit_at_root_field:      return "emit_at_root_field";
  case bc_opcode::emit_at_root_field_raw:  return "emit_at_root_field_raw";
  case bc_opcode::emit_at_key:             return "emit_at_key";
  case bc_opcode::emit_this:               return "emit_this";
  case bc_opcode::resolve_filtered:        return "resolve_filtered";
  case bc_opcode::filter_upper:            return "filter_upper";
  case bc_opcode::filter_lower:            return "filter_lower";
  case bc_opcode::filter_capitalize:       return "filter_capitalize";
  case bc_opcode::filter_title:            return "filter_title";
  case bc_opcode::filter_trim:             return "filter_trim";
  case bc_opcode::filter_ltrim:            return "filter_ltrim";
  case bc_opcode::filter_rtrim:            return "filter_rtrim";
  case bc_opcode::filter_left:             return "filter_left";
  case bc_opcode::filter_right:            return "filter_right";
  case bc_opcode::filter_center:           return "filter_center";
  case bc_opcode::filter_truncate:         return "filter_truncate";
  case bc_opcode::filter_substr:           return "filter_substr";
  case bc_opcode::filter_replace:          return "filter_replace";
  case bc_opcode::filter_default:          return "filter_default";
  case bc_opcode::filter_json:             return "filter_json";
  case bc_opcode::filter_safe:             return "filter_safe";
  case bc_opcode::filter_indent:           return "filter_indent";
  case bc_opcode::filter_pad:              return "filter_pad";
  case bc_opcode::filter_pluralize:        return "filter_pluralize";
  case bc_opcode::emit_filtered:           return "emit_filtered";
  case bc_opcode::emit_filtered_raw:       return "emit_filtered_raw";
  case bc_opcode::filter_int_abs:          return "filter_int_abs";
  case bc_opcode::filter_int_hex:          return "filter_int_hex";
  case bc_opcode::filter_int_oct:          return "filter_int_oct";
  case bc_opcode::filter_int_bin:          return "filter_int_bin";
  case bc_opcode::filter_int_neg:          return "filter_int_neg";
  case bc_opcode::filter_int_mod:          return "filter_int_mod";
  case bc_opcode::filter_int_numify:       return "filter_int_numify";
  case bc_opcode::filter_int_is_neg:       return "filter_int_is_neg";
  case bc_opcode::filter_int_eq:           return "filter_int_eq";
  case bc_opcode::filter_int_ne:           return "filter_int_ne";
  case bc_opcode::filter_int_gt:           return "filter_int_gt";
  case bc_opcode::filter_int_gte:          return "filter_int_gte";
  case bc_opcode::filter_int_lt:           return "filter_int_lt";
  case bc_opcode::filter_int_lte:          return "filter_int_lte";
  case bc_opcode::filter_int_zerofill:     return "filter_int_zerofill";
  case bc_opcode::filter_int_add:          return "filter_int_add";
  case bc_opcode::filter_int_sub:          return "filter_int_sub";
  case bc_opcode::filter_int_mul:          return "filter_int_mul";
  case bc_opcode::filter_int_div:          return "filter_int_div";
  case bc_opcode::filter_float_precision:  return "filter_float_precision";
  case bc_opcode::emit_if_filtered:        return "emit_if_filtered";
  case bc_opcode::emit_break:              return "emit_break";
  case bc_opcode::emit_continue:           return "emit_continue";
  case bc_opcode::emit_at_index1:          return "emit_at_index1";
  case bc_opcode::emit_at_size:            return "emit_at_size";
  case bc_opcode::emit_var_size:           return "emit_var_size";
  case bc_opcode::emit_if_or:              return "emit_if_or";
  case bc_opcode::emit_if_and:             return "emit_if_and";
  case bc_opcode::emit_if_not:             return "emit_if_not";
  case bc_opcode::call_partial:            return "call_partial";
  case bc_opcode::halt:                    return "halt";
  }
  return "unknown";
}

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
  substr,      /**< 部分文字列（引数1: 開始位置, 引数2: 文字数） */
  replace,     /**< 部分文字列置換 */
  default_value, /**< デフォルト値（引数: フォールバック文字列） */
  to_json,     /**< JSON出力 */
  safe,        /**< 生出力マーク（エスケープ抑制、コンパイル時処理） */
  indent,      /**< 各行にインデント追加（引数: 空白数） */
  pad,         /**< パディング（引数1: 幅, 引数2: 埋め文字） */
  pluralize    /**< 単数形/複数形（引数1: 単数形, 引数2: 複数形） */
};

[[nodiscard]] inline std::string_view string_filter_name(string_filter f) noexcept {
  switch (f) {
  case string_filter::upper:      return "upper";
  case string_filter::lower:      return "lower";
  case string_filter::capitalize: return "capitalize";
  case string_filter::title:      return "title";
  case string_filter::trim:       return "trim";
  case string_filter::ltrim:      return "ltrim";
  case string_filter::rtrim:      return "rtrim";
  case string_filter::left:       return "left";
  case string_filter::right:      return "right";
  case string_filter::center:     return "center";
  case string_filter::truncate:   return "truncate";
  case string_filter::substr:        return "substr";
  case string_filter::replace:       return "replace";
  case string_filter::default_value: return "default";
  case string_filter::to_json:       return "json";
  case string_filter::safe:          return "safe";
  case string_filter::indent:        return "indent";
  case string_filter::pad:           return "pad";
  case string_filter::pluralize:     return "pluralize";
  }
  return "unknown";
}

/**
 * @brief 文字列フィルタエントリ（引数付き）
 * @details フィルタの種別と、引数を必要とするフィルタの幅/最大文字数を保持する
 */
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

[[nodiscard]] inline std::string_view int_filter_name(int_filter f) noexcept {
  switch (f) {
  case int_filter::abs:     return "abs";
  case int_filter::hex:     return "hex";
  case int_filter::oct:     return "oct";
  case int_filter::bin:     return "bin";
  case int_filter::neg:     return "neg";
  case int_filter::mod:     return "mod";
  case int_filter::numify:  return "numify";
  case int_filter::is_neg:  return "is_neg";
  case int_filter::eq:      return "eq";
  case int_filter::ne:      return "ne";
  case int_filter::gt:      return "gt";
  case int_filter::gte:     return "gte";
  case int_filter::lt:      return "lt";
  case int_filter::lte:     return "lte";
  case int_filter::zerofill: return "zerofill";
  case int_filter::add:      return "add";
  case int_filter::sub:      return "sub";
  case int_filter::mul:      return "mul";
  case int_filter::div:      return "div";
  }
  return "unknown";
}

/**
 * @brief 整数フィルタエントリ（引数付き）
 * @details フィルタの種別と、引数を必要とするフィルタの値を保持する
 */
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

[[nodiscard]] inline std::string_view float_filter_name(float_filter f) noexcept {
  switch (f) {
  case float_filter::precision: return "precision";
  }
  return "unknown";
}

/**
 * @brief 実数フィルタエントリ（引数付き）
 * @details フィルタの種別と、引数を必要とするフィルタの値を保持する
 */
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
  compare_operand_kind compare_rhs_kind = compare_operand_kind::none; /**< if 比較の右オペランド種別 */
  std::string compare_rhs_text;            /**< 右オペランド文字列（文字列リテラル or 変数名） */
  std::uint32_t compare_rhs_field_index = UINT32_MAX; /**< 右オペランドの解決済みフィールドインデックス */
  bool compare_rhs_has_dot = false;        /**< 右オペランドがドット区切りパスか */
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
  [[nodiscard]] std::string disassemble() const {
    std::string out;
    auto append = [&](std::string_view sv) {
      out.append(sv);
    };

    // 命令列
    append("--- instructions ---\n");
    for (std::size_t i = 0; i < instructions.size(); ++i) {
      auto const& instr = instructions[i];
      auto addr = static_cast<std::uint32_t>(i);

      // アドレス
      char addr_buf[16];
      auto [p1, ec1] = std::to_chars(addr_buf, addr_buf + sizeof(addr_buf), addr);
      append(std::string_view{addr_buf, static_cast<std::size_t>(p1 - addr_buf)});
      append("  ");

      // オペコード名
      append(opcode_name(instr.op));
      auto pad = 22 - (p1 - addr_buf) - opcode_name(instr.op).size();
      for (std::size_t j = 0; j < pad; ++j) {
        out.push_back(' ');
      }

      // オペランド（種類に応じて解釈）
      switch (instr.op) {
      case bc_opcode::emit_literal: {
        if (instr.operand < literals.size()) {
          append("\"");
          append(literals[instr.operand]);
          append("\"");
        } else {
          char buf[16];
          auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
          append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        }
        break;
      }
      case bc_opcode::emit_var:
      case bc_opcode::emit_var_raw:
      case bc_opcode::emit_section:
      case bc_opcode::emit_inverted:
      case bc_opcode::emit_at_index:
      case bc_opcode::emit_at_first:
      case bc_opcode::emit_at_last:
      case bc_opcode::emit_at_section:
      case bc_opcode::emit_at_inverted:
      case bc_opcode::emit_at_key:
      case bc_opcode::emit_this:
      case bc_opcode::emit_break:
      case bc_opcode::emit_continue: {
        if (instr.operand2 < var_refs.size()) {
          append(var_refs[instr.operand2].key);
          if (instr.op == bc_opcode::emit_section || instr.op == bc_opcode::emit_inverted || instr.op == bc_opcode::emit_at_section || instr.op == bc_opcode::emit_at_inverted) {
            append("  -> ");
            char buf[16];
            auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
            append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
            if (instr.operand3 != 0 && (instr.op == bc_opcode::emit_section || instr.op == bc_opcode::emit_inverted)) {
              append("  else_target=");
              auto [p2, ec2] = std::to_chars(buf, buf + sizeof(buf), instr.operand3);
              append(std::string_view{buf, static_cast<std::size_t>(p2 - buf)});
            }
          } else {
            append("  loop#");
            char buf[16];
            auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand2);
            append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
          }
          break;
        } else {
          char buf[16];
          auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand2);
          append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        }
        break;
      }
      case bc_opcode::emit_at_index1: {
        char buf[16];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand2);
        append("@index1 (loop#");
        append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        append(")");
        break;
      }
      case bc_opcode::emit_litvar:
      case bc_opcode::emit_litvar_raw: {
        // operand = リテラルインデックス, operand2 = 変数参照インデックス
        if (instr.operand < literals.size()) {
          append("\"");
          append(literals[instr.operand]);
          append("\" ");
        }
        if (instr.operand2 < var_refs.size()) {
          append("+ ");
          append(var_refs[instr.operand2].key);
        }
        break;
      }
      case bc_opcode::emit_if:
      case bc_opcode::emit_else:
      case bc_opcode::emit_endif: {
        if (instr.operand2 < var_refs.size()) {
          append(var_refs[instr.operand2].key);
          append("  -> ");
        }
        char buf[16];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
        append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        break;
      }
      case bc_opcode::emit_end: {
        append("-> ");
        char buf[16];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
        append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        break;
      }
      case bc_opcode::emit_at_root:
      case bc_opcode::emit_at_root_field:
      case bc_opcode::emit_at_root_field_raw: {
        if (instr.operand2 < var_refs.size()) {
          append(var_refs[instr.operand2].key);
        }
        break;
      }
      case bc_opcode::resolve_filtered:
      case bc_opcode::emit_filtered:
      case bc_opcode::emit_filtered_raw:
      case bc_opcode::emit_if_filtered: {
        if (instr.operand2 < var_refs.size()) {
          append(var_refs[instr.operand2].key);
        }
        break;
      }
      case bc_opcode::filter_left:
      case bc_opcode::filter_right:
      case bc_opcode::filter_center:
      case bc_opcode::filter_truncate:
      case bc_opcode::filter_int_mod:
      case bc_opcode::filter_int_add:
      case bc_opcode::filter_int_sub:
      case bc_opcode::filter_int_mul:
      case bc_opcode::filter_int_div: {
        char buf[16];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
        append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        break;
      }
      case bc_opcode::filter_substr: {
        append("start=");
        char buf[16];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
        append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        append(" len=");
        auto [p2, ec2] = std::to_chars(buf, buf + sizeof(buf), instr.operand2);
        append(std::string_view{buf, static_cast<std::size_t>(p2 - buf)});
        break;
      }
      case bc_opcode::call_partial: {
        if (instr.operand < partial_entries.size()) {
          append("partial=\"");
          append(partial_entries[instr.operand].name);
          append("\"");
        } else {
          char buf[16];
          auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
          append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        }
        break;
      }
      case bc_opcode::filter_default: {
        if (instr.operand < literals.size()) {
          append("\"");
          append(literals[instr.operand]);
          append("\"");
        }
        break;
      }
      case bc_opcode::filter_pad: {
        char buf[16];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
        append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        if (instr.operand2 < literals.size()) {
          append(" \"");
          append(literals[instr.operand2]);
          append("\"");
        }
        break;
      }
      case bc_opcode::filter_pluralize: {
        if (instr.operand < literals.size()) {
          append("\"");
          append(literals[instr.operand]);
          append("\"");
        }
        if (instr.operand2 < literals.size()) {
          append(" \"");
          append(literals[instr.operand2]);
          append("\"");
        }
        break;
      }
      case bc_opcode::filter_float_precision:
      case bc_opcode::filter_int_eq:
      case bc_opcode::filter_int_ne:
      case bc_opcode::filter_int_gt:
      case bc_opcode::filter_int_gte:
      case bc_opcode::filter_int_lt:
      case bc_opcode::filter_int_lte:
      case bc_opcode::filter_int_zerofill: {
        char buf[16];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
        append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        break;
      }
      default:
        break;
      }
      append("\n");
    }

    // リテラルテーブル
    if (!literals.empty()) {
      append("\n--- literals ---\n");
      for (std::size_t i = 0; i < literals.size(); ++i) {
        char addr_buf[16];
        auto [p, ec] = std::to_chars(addr_buf, addr_buf + sizeof(addr_buf), i);
        append(std::string_view{addr_buf, static_cast<std::size_t>(p - addr_buf)});
        append(": \"");
        append(literals[i]);
        append("\"\n");
      }
    }

    // 変数参照テーブル
    if (!var_refs.empty()) {
      append("\n--- var_refs ---\n");
      for (std::size_t i = 0; i < var_refs.size(); ++i) {
        char addr_buf[16];
        auto [p, ec] = std::to_chars(addr_buf, addr_buf + sizeof(addr_buf), i);
        append(std::string_view{addr_buf, static_cast<std::size_t>(p - addr_buf)});
        append(": ");
        append(var_refs[i].key);
        if (var_refs[i].field_index != UINT32_MAX) {
          append("  field=");
          char fbuf[16];
          auto [fp, fec] = std::to_chars(fbuf, fbuf + sizeof(fbuf), var_refs[i].field_index);
          append(std::string_view{fbuf, static_cast<std::size_t>(fp - fbuf)});
        }
        if (!var_refs[i].filters.empty()) {
          append("  filters=[");
          for (std::size_t j = 0; j < var_refs[i].filters.size(); ++j) {
            if (j > 0) {
              append(", ");
            }
            append(string_filter_name(var_refs[i].filters[j].filter));
          }
          append("]");
        }
        if (!var_refs[i].int_filters.empty()) {
          append("  int_filters=[");
          for (std::size_t j = 0; j < var_refs[i].int_filters.size(); ++j) {
            if (j > 0) {
              append(", ");
            }
            append(int_filter_name(var_refs[i].int_filters[j].filter));
          }
          append("]");
        }
        if (!var_refs[i].float_filters.empty()) {
          append("  float_filters=[");
          for (std::size_t j = 0; j < var_refs[i].float_filters.size(); ++j) {
            if (j > 0) {
              append(", ");
            }
            append(float_filter_name(var_refs[i].float_filters[j].filter));
          }
          append("]");
        }
        append("\n");
      }
    }

    // partial テーブル
    if (!partial_entries.empty()) {
      append("\n--- partials ---\n");
      for (std::size_t i = 0; i < partial_entries.size(); ++i) {
        char addr_buf[16];
        auto [p, ec] = std::to_chars(addr_buf, addr_buf + sizeof(addr_buf), i);
        append(std::string_view{addr_buf, static_cast<std::size_t>(p - addr_buf)});
        append(": \"");
        append(partial_entries[i].name);
        append("\" (");
        auto [p2, ec2] = std::to_chars(addr_buf, addr_buf + sizeof(addr_buf), partial_entries[i].bc->instructions.size());
        append(std::string_view{addr_buf, static_cast<std::size_t>(p2 - addr_buf)});
        append(" instr)\n");
      }
    }

    return out;
  }
};

} // namespace injamm::detail
