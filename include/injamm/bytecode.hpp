#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "bytecode_fwd.hpp"
#include "types.hpp"

namespace injamm::detail {

struct string_filter_entry {
  string_filter filter;       /**< フィルタの種別 */
  int arg1 = 0;               /**< 第1引数（left/right/center/truncate/substr の幅/開始位置） */
  int arg2 = 0;               /**< 第2引数（substr の文字数） */
  std::string_view str_arg1;  /**< 文字列引数1（replace の old 文字列） */
  std::string_view str_arg2;  /**< 文字列引数2（replace の new 文字列） */
};

struct int_filter_entry {
  int_filter filter; /**< フィルタの種別 */
  int arg = 0;       /**< 引数（mod の除数） */
};

struct float_filter_entry {
  float_filter filter; /**< フィルタの種別 */
  int arg = 0;         /**< 引数（precision の小数点以下桁数） */
};

/**
 * @brief 特殊変数の種別（コンパイル時分類）
 * @details this / loop.* / loop.parent.* をコンパイル時に分類し、
 *          実行時ホットパスの文字列比較を整数比較に置き換えるための enum。
 *          *_unknown は該当プレフィックスだがプロパティ名が不明なもの
 *          （実行時は偽扱い）を表す。
 */
enum class special_var_kind : std::uint8_t {
  none,
  this_,
  loop_index,
  loop_index1,
  loop_size,
  loop_is_first,
  loop_is_last,
  loop_key,
  loop_unknown,
  lp_index,
  lp_index1,
  lp_size,
  lp_is_first,
  lp_is_last,
  lp_key,
  lp_unknown,
};

/** @brief 変数キーを special_var_kind に分類する */
inline special_var_kind classify_special_var(std::string_view key) {
  if (key == "this") {
    return special_var_kind::this_;
  }
  if (!key.starts_with("loop.")) {
    return special_var_kind::none;
  }
  auto prop = key.substr(5);
  if (prop.starts_with("parent.")) {
    auto p = prop.substr(7);
    if (p == "index") return special_var_kind::lp_index;
    if (p == "index1") return special_var_kind::lp_index1;
    if (p == "size") return special_var_kind::lp_size;
    if (p == "is_first") return special_var_kind::lp_is_first;
    if (p == "is_last") return special_var_kind::lp_is_last;
    if (p == "key") return special_var_kind::lp_key;
    return special_var_kind::lp_unknown;
  }
  if (prop == "index") return special_var_kind::loop_index;
  if (prop == "index1") return special_var_kind::loop_index1;
  if (prop == "size") return special_var_kind::loop_size;
  if (prop == "is_first") return special_var_kind::loop_is_first;
  if (prop == "is_last") return special_var_kind::loop_is_last;
  if (prop == "key") return special_var_kind::loop_key;
  return special_var_kind::loop_unknown;
}

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
  special_var_kind special = special_var_kind::none; /**< コンパイル時解決: this / loop.* / loop.parent.* の分類 */
  bool binding_first = false;              /**< コンパイル時解決: キーが内包セクションの束縛参照であることが確定しているか（サブパスの field_index 事前解決の根拠） */
  std::uint8_t path_hint_len = 0;          /**< path_indices の有効長 */
  /** @brief コンパイル時解決: ドット区切りパスの階層別フィールドインデックス
   *  @details ヒープを避けるためインライン固定長（4階層まで）。実行時は名前検証付きで使用するため
   *           途中までの解決や誤解決でも安全（不一致なら線形探索にフォールバック）。 */
  std::array<std::uint32_t, 4> path_indices{};
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
    ref.special = classify_special_var(key);
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
