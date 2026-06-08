#pragma once

#include <string_view>

namespace injamm::detail {

/** @brief HTMLエスケープをバッファに書き込む
 *
 *  @tparam Buffer 出力バッファ型（std::string など）
 *  @param[in,out] out 出力先バッファ
 *  @param[in] s HTMLエスケープする文字列
 *
 *  @details <, >, &, ", ' の5文字をそれぞれのHTMLエンティティに変換して追記する。
 *  バッファの効率を考慮し、出力バッファを直接受け取る設計としている。
 */
template <class Buffer>
inline void html_escape_into(Buffer& out, std::string_view s) {
  for (auto const c : s) {
    switch (c) {
      case '<':
        out.append("&lt;");
        break;
      case '>':
        out.append("&gt;");
        break;
      case '&':
        out.append("&amp;");
        break;
      case '"':
        out.append("&quot;");
        break;
      case '\'':
        out.append("&#x27;");
        break;
      default:
        out.push_back(c);
        break;
    }
  }
}

} // namespace injamm::detail
