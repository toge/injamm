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
 *  特殊文字を含まない連続区間は一括 append し、push_back の呼び出し回数を削減する。
 */
template <class Buffer>
inline void html_escape_into(Buffer& out, std::string_view s) {
  std::size_t safe = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    auto const c = s[i];
    if (c != '<' && c != '>' && c != '&' && c != '"' && c != '\'') continue;
    if (i > safe) out.append(s.data() + safe, i - safe);
    switch (c) {
      case '<': out.append("&lt;"); break;
      case '>': out.append("&gt;"); break;
      case '&': out.append("&amp;"); break;
      case '"': out.append("&quot;"); break;
      case '\'': out.append("&#x27;"); break;
    }
    safe = i + 1;
  }
  if (safe < s.size()) out.append(s.data() + safe, s.size() - safe);
}

} // namespace injamm::detail
