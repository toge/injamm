#pragma once
/**
 * @file render.hpp
 * @brief injamm_codegen によって自動生成されたレンダリング関数
 */

#include <charconv>
#include <cctype>
#include <cmath>
#include <expected>
#include <string>
#include <string_view>

#include <injamm/types.hpp>

namespace generated {

/** @brief HTML エスケープ関数 */
inline void html_escape_append(std::string& out, std::string_view sv) {
  for (char c : sv) {
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;       break;
    }
  }
}

/** @brief 整数→文字列変換ヘルパ */
template <typename N>
inline void append_number(std::string& out, N n) {
  char buf[64];
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), n);
  out.append(buf, ptr);
}

/** @brief 値を文字列に変換して追加（数値/bool/文字列に対応） */
template <typename V>
inline void append_value(std::string& out, V const& v) {
  if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, std::string_view>) {
    out += v;
  } else if constexpr (std::is_same_v<V, const char*>) {
    out += v ? v : "";
  } else if constexpr (std::is_same_v<V, bool>) {
    out += v ? "true" : "false";
  } else if constexpr (std::is_arithmetic_v<V>) {
    append_number(out, v);
  } else {
    out += v;
  }
}

/** @brief 値を HTML エスケープして追加 */
template <typename V>
inline void html_escape_append_value(std::string& out, V const& v) {
  if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, std::string_view>) {
    html_escape_append(out, v);
  } else if constexpr (std::is_same_v<V, const char*>) {
    html_escape_append(out, v ? v : "");
  } else if constexpr (std::is_same_v<V, bool>) {
    html_escape_append(out, v ? "true" : "false");
  } else if constexpr (std::is_arithmetic_v<V>) {
    std::string tmp;
    append_number(tmp, v);
    html_escape_append(out, tmp);
  } else {
    html_escape_append(out, v);
  }
}

/** @brief 文字列の大文字変換 */
inline void filter_to_upper(std::string& s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

/** @brief 文字列の小文字変換 */
inline void filter_to_lower(std::string& s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

/** @brief 文字列の先頭大文字変換 */
inline void filter_capitalize(std::string& s) {
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
}

/** @brief 文字列のトリム */
inline void filter_trim(std::string& s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
}

/** @brief 文字列の左トリム */
inline void filter_ltrim(std::string& s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
}

/** @brief 文字列の右トリム */
inline void filter_rtrim(std::string& s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
}

/** @brief 文字列の切り詰め */
inline void filter_truncate(std::string& s, int max_len) {
  if (static_cast<int>(s.size()) > max_len) s.resize(static_cast<std::size_t>(max_len));
}

/** @brief 部分文字列 */
inline void filter_substr(std::string& s, int pos, int len) {
  if (pos < 0) pos = 0;
  if (pos >= static_cast<int>(s.size())) { s.clear(); return; }
  s = s.substr(static_cast<std::size_t>(pos), static_cast<std::size_t>(len));
}

/** @brief 整数のカンマ区切り */
inline void filter_numify(std::string& s) {
  if (s.size() <= 3) return;
  std::string result;
  int count = 0;
  for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
    if (count == 3) { result = ',' + result; count = 0; }
    result = s[static_cast<std::size_t>(i)] + result;
    ++count;
  }
  s = std::move(result);
}

/** @brief 整数の0埋め */
inline void filter_zerofill(std::string& s, int width) {
  if (static_cast<int>(s.size()) < width)
    s = std::string(static_cast<std::size_t>(width) - s.size(), '0') + s;
}


/** @brief テンプレート文字列から生成されたレンダリング関数 */
template <typename T>
[[nodiscard]] std::expected<std::string, injamm::error_ctx>
render(const T& data) {
  std::string out;
  out.reserve(256);
  
  out += "Order #";
  html_escape_append_value(out, data.order_id);
  out += ":\n";
  if (data.total > 1000) {
    out += "[VIP]";
  }
  out += "\n";
  for (std::size_t _i1 = 0; _i1 < data.items.size(); ++_i1) {
    const auto& _item1 = data.items[_i1];
    out += "\n  ";
    html_escape_append_value(out, _item1.name);
    out += ": $";
    html_escape_append_value(out, _item1.price);
    out += "\n";
  }
  out += "\nTotal: $";
  html_escape_append_value(out, data.total);
  
  return out;
}

} // namespace generated
