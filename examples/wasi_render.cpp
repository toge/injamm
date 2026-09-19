#include "injamm.hpp"
#include <glaze/glaze.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief WASI サーバサイド描画デモ
 * @details argv[1] のテンプレート文字列を実行時コンパイル (Bytecode VM) して
 *          固定デモデータで描画し、結果を stdout に書き出す。
 *          wasmtime での実行を想定: `wasmtime run injamm_wasi_render -- "Hello {{title}}"`。
 *          bytecode→WASM 翻訳なしに VM ごと WASM 化できることの end-to-end 証明。
 */

/** @brief デモ用ユーザーデータ */
struct wasi_user {
  std::string name;  /**< ユーザー名 */
  int         age{}; /**< 年齢 */
};

/** @brief デモ用トップレベルデータ */
struct wasi_data {
  std::string            title;  /**< タイトル */
  std::vector<wasi_user> users;  /**< ユーザー配列 */
  bool                   show{}; /**< if セクション用フラグ */
};

int main(int argc, char** argv) {
  std::string_view tmpl = argc > 1 ? std::string_view{argv[1]}
                                   : std::string_view{"{{title}}: {{#users}}{{name}}({{age}}){{/users}}"
                                                      "{{#if show}} [shown]{{/if}}|{{> user}}"};
  const wasi_data  data{"team", {{"Alice", 30}, {"Bob", 20}}, true};

  // partial "user" を登録 ({{> user}} 用)
  std::vector<injamm::detail::partial_entry> partials;
  partials.push_back(injamm::make_partial<wasi_data>("user", "{{#users}}{{name | upper}};{{/users}}"));

  const injamm::engine<wasi_data> eng{tmpl, std::move(partials)};
  auto                            r = eng.render(data);
  if (!r) {
    std::printf("error: ec=%d pos=%zu\n", static_cast<int>(r.error().ec), r.error().position);
    return 1;
  }
  std::fwrite(r->data(), 1, r->size(), stdout);
  std::fputc('\n', stdout);
  return 0;
}
