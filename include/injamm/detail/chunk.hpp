#pragma once

#include <string>
#include <variant>
#include <vector>

namespace injamm {

/**
 * @brief 前方宣言
 *
 * @details chunk_section::body および chunk_inverted::body で使用するため、
 *          parsed_template クラスを事前宣言する。
 */
class parsed_template;

namespace detail {

/**
 * @brief リテラルテキストチャンク
 *
 * @details テンプレート中に記述された生のテキスト断片を保持する。
 *          レンダリング時にはそのまま出力文字列にコピーされる。
 */
struct chunk_literal {
  std::string text;
};

/**
 * @brief プレースホルダーチャンク（ {{ key }} / {{{ key }}} ）
 *
 * @details テンプレート変数への参照を保持する。
 *          二重波括弧の場合は HTML エスケープ付き、
 *          三重波括弧の場合は生出力（ステンシルモード）となる。
 */
struct chunk_placeholder {
  std::string key;
  bool raw = false; /**< @brief true の場合 HTML エスケープなし（{{{...}}} 記法） */
};

/**
 * @brief セクションチャンク（ {{#key}}...{{/key}} ）
 *
 * @details コンテキスト値が truthy の場合に body が描画される。
 *          入れ子テンプレートを vector で保持し、再帰的にレンダリングする。
 */
struct chunk_section {
  std::string key;
  std::vector<parsed_template> body;
};

/**
 * @brief 逆セクションチャンク（ {{^key}}...{{/key}} ）
 *
 * @details コンテキスト値が falsy または空の場合に body が描画される。
 *          いわゆる「not セクション」として動作する。
 */
struct chunk_inverted {
  std::string key;
  std::vector<parsed_template> body;
};

/**
 * @brief @変数チャンク（ {{@index}}, {{@first}}, {{@last}}, {{@root}} など）
 *
 * @details セクション内で暗黙的に利用可能な特殊変数を表す。
 *          @index, @first, @last はループ内での位置情報、
 *          @root はコンテキストルートへの参照として機能する。
 */
struct chunk_at_var {
  enum class kind { index, first, last, root };
  kind var;
};

/**
 * @brief @変数セクションチャンク（ {{#@last}}...{{/@last}} ）
 *
 * @details @変数をセクション条件として使用する。
 *          inverted が true の場合は逆条件（{{^@last}} 相当）となる。
 */
struct chunk_at_section {
  chunk_at_var::kind var;
  std::vector<parsed_template> body;
  bool inverted = false; /**< @brief true の場合は逆セクションとして動作 */
};

/**
 * @brief if/else チャンク（ {{#if X}}...{{else}}...{{/if}} ）
 *
 * @details 条件式 X を評価し、truthy なら then_branch、falsy なら else_branch を描画する。
 *          else 節は省略可能。
 */
struct chunk_if {
  std::string expr;
  std::vector<parsed_template> then_branch;
  std::vector<parsed_template> else_branch;
};

/**
 * @brief すべてのチャンク型のバリアント
 *
 * @details parsed_template が保持する要素はこのバリアントのいずれか一つである。
 *          実行時のチャンク種別は variant::index() や std::visit で判定する。
 */
using chunk = std::variant<
    chunk_literal,
    chunk_placeholder,
    chunk_section,
    chunk_inverted,
    chunk_at_var,
    chunk_at_section,
    chunk_if>;

/**
 * @brief チャンクリストを各1チャンクの parsed_template にラップする
 *
 * @details wrap_body_chunks はパーサーから渡されたチャンク vector を、
 *          各チャンクが1つだけ格納された parsed_template の vector に変換する。
 *          これは parsed_template::chunks のサイズと実際のチャンク数を一致させるために必要。
 *
 * @param chunks ラップ対象のチャンクリスト
 * @return std::vector<parsed_template> 各要素が1チャンクを持つ parsed_template の配列
 */
[[nodiscard]] constexpr auto wrap_body_chunks(std::vector<chunk> chunks) -> std::vector<parsed_template>;

} // namespace detail

/**
 * @brief パース済みテンプレート
 *
 * @details テンプレート文字列をパースした結果のチャンク列を保持する。
 *          実行時レンダリング（Bytecode VM）の基本単位となる。
 */
class parsed_template {
public:
  std::vector<detail::chunk> chunks;
};

namespace detail {

constexpr auto wrap_body_chunks(std::vector<chunk> chunks) -> std::vector<parsed_template> {
  std::vector<parsed_template> result;
  result.reserve(chunks.size());
  for (auto& c : chunks) {
    parsed_template pt;
    pt.chunks.push_back(std::move(c));
    result.push_back(std::move(pt));
  }
  return result;
}

} // namespace detail

} // namespace injamm
