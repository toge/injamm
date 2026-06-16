#include "injamm.hpp"
#include <glaze/glaze.hpp>
#include <iostream>
#include <vector>

/**
 * @brief サンプルアプリケーションのユーザーデータ型
 * @details 名前と年齢を保持する単純な構造体。engine および render の両 API で使用する。
 */
struct User {
  std::string name; /**< ユーザー名 */
  int age{};        /**< 年齢 */
};

/**
 * @brief サンプルアプリケーションのトップレベルデータ型
 * @details User の配列を保持し、Bytecode VM のセクション描画の実演に使用する。
 */
struct Data {
  std::vector<User> users; /**< ユーザーの配列 */
};

/** @brief Glaze メタ情報: User の JSON シリアライズ定義 */
template <>
struct glz::meta<User> {
  static constexpr auto value = glz::object("name", &User::name, "age", &User::age);
};

/** @brief Glaze メタ情報: Data の JSON シリアライズ定義 */
template <>
struct glz::meta<Data> {
  static constexpr auto value = glz::object("users", &Data::users);
};

/**
 * @brief エントリポイント
 * @details injamm が提供する 2 つのレンダリング API（Bytecode VM と NTTP コンパイル時）の
 *          使用例を示す。3 人のユーザーデータに対してテンプレート描画を実行する。
 * @return int 常に 0 を返す
 */
int main() {
  Data data{{{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}}};

  /**
   * API 1: Bytecode VM（実行時テンプレートコンパイル、全機能対応）
   * @details engine はテンプレート文字列をバイトコードにコンパイルし、
   *          任意のコンテキストデータで描画する。セクション、if/else、@index/@first/@last、
   *          ネストパスなどの全機能が利用可能。
   */
  auto bc = injamm::engine<Data>("Users: {{#users}}{{name}} ({{age}})"
                                       "{{#if @last}}.{{else}}, {{/if}}{{/users}}");
  auto r1 = bc.render(data);
  if (r1) {
    std::cout << "Bytecode VM: " << *r1 << "\n";
  } else {
    std::cerr << "Bytecode VM error: " << static_cast<int>(r1.error().ec) << "\n";
  }

  /**
   * API 2: NTTP コンパイル時レンダリング（全機能対応）
   * @details render<fixed_string> はテンプレート文字列を NTTP（Non-Type Template Parameter）
   *          として受け取り、コンパイル時にテンプレート解析を行う。
   *          セクション、if/else、フィルター、break/continue を含む全機能に対応。
   */
  auto constexpr kTmpl = injamm::fixed_string("Hello {{name}}! You are {{age}}.");
  auto r2 = injamm::render<kTmpl>(User{"Bob", 25});
  std::cout << "NTTP render: " << (r2 ? *r2 : "ERROR") << "\n";

  return 0;
}
