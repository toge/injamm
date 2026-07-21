#pragma once
#ifndef INJAMM_CODEGEN_HELPERS_HPP
#define INJAMM_CODEGEN_HELPERS_HPP

#include <charconv>
#include <cctype>
#include <string>
#include <string_view>
#include <type_traits>

namespace generated {

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

template <typename N>
inline void append_number(std::string& out, N n) {
  char buf[64];
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), n);
  out.append(buf, ptr);
}

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

inline void filter_to_upper(std::string& s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

inline void filter_to_lower(std::string& s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline void filter_capitalize(std::string& s) {
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
}

inline void filter_trim(std::string& s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
}

inline void filter_ltrim(std::string& s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
}

inline void filter_rtrim(std::string& s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
}

inline void filter_truncate(std::string& s, int max_len) {
  if (static_cast<int>(s.size()) > max_len) s.resize(static_cast<std::size_t>(max_len));
}

inline void filter_substr(std::string& s, int pos, int len) {
  if (pos < 0) pos = 0;
  if (pos >= static_cast<int>(s.size())) { s.clear(); return; }
  s = s.substr(static_cast<std::size_t>(pos), static_cast<std::size_t>(len));
}

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

inline void filter_zerofill(std::string& s, int width) {
  if (static_cast<int>(s.size()) < width)
    s = std::string(static_cast<std::size_t>(width) - s.size(), '0') + s;
}

} // namespace generated

#endif // INJAMM_CODEGEN_HELPERS_HPP
