/**
 * @brief ループ内配列名束縛（複数配列の並行ループ）の使用例
 *
 * @details row (1..9) と col (1..9) を渡し、入れ子ループで
 *          "r×c" 形式の 9×9 表を出力する。
 *          ループ内では配列名（row, col）が「現在要素」を指す。
 */

#include "injamm.hpp"
#include <glaze/glaze.hpp>
#include <iostream>
#include <vector>

struct ZipCtx {
  std::vector<int> row;
  std::vector<int> col;
};

template <>
struct glz::meta<ZipCtx> {
  static constexpr auto value = glz::object("row", &ZipCtx::row, "col", &ZipCtx::col);
};

int main() {
  ZipCtx ctx;
  for (int i = 1; i <= 9; ++i) {
    ctx.row.push_back(i);
    ctx.col.push_back(i);
  }

  // ループ内で {{row}} は外側ループの現在要素、{{col}} は内側ループの現在要素
  auto engine = injamm::engine<ZipCtx>(
      "{{#row}}{{#col}}{{row}}×{{col}} {{/col}}\n{{/row}}");
  auto r = engine.render(ctx);
  if (r) {
    std::cout << *r;
  } else {
    std::cerr << "error: " << static_cast<int>(r.error().ec) << "\n";
    return 1;
  }

  return 0;
}
