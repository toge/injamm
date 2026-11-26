/**
 * @file test_codegen_nosimd.cpp
 * @brief `--no-simd` 生成コード用ヘルパの回帰テスト
 *
 * @details INJAMM_CODEGEN_DISABLE_SIMD 時の HTML エスケープと、
 *          LLONG_MIN 境界の整数フィルタが VM と同じ結果になることを確認する。
 */

#define INJAMM_CODEGEN_DISABLE_SIMD 1
#include "codegen_helpers.hpp"

#include <iostream>
#include <limits>
#include <string>

namespace {
int failures = 0;

void check(bool ok, char const* what) {
  if (!ok) {
    std::cerr << "FAIL: " << what << "\n";
    ++failures;
  }
}
} // namespace

int main() {
  // single quote は VM (escape.hpp) と同じ &#x27; で出力する
  {
    std::string out;
    html_escape_append(out, "<a b='c'>&\"</a>");
    check(out == "&lt;a b=&#x27;c&#x27;&gt;&amp;&quot;&lt;/a&gt;", "no-simd escape");
  }

  // LLONG_MIN の mod / div / bin
  {
    std::string s = "-9223372036854775808";
    filter_int_mod(s, -1);
    check(s == "0", "mod LLONG_MIN");
  }
  {
    std::string s = "-9223372036854775808";
    filter_int_div(s, -1);
    check(s == "9223372036854775808", "div LLONG_MIN");
  }
  {
    std::string s = "-9223372036854775808";
    filter_int_bin(s);
    check(s == "-1000000000000000000000000000000000000000000000000000000000000000", "bin LLONG_MIN");
  }

  {
    char text[] = "ok";
    char const* const_text = text;
    char const raw[] = "raw";
    static_assert(codegen_c_string_pointer_v<decltype(const_text)>);
    static_assert(codegen_c_string_pointer_v<char*>);
    static_assert(!codegen_c_string_pointer_v<decltype(raw)>);
    static_assert(codegen_unsupported_string_v<decltype(raw)>);
    static_assert(!codegen_c_string_pointer_v<volatile char*>);
    static_assert(codegen_unsupported_string_v<volatile char*>);
    std::string out;
    append_value(out, const_text);
    check(out == "ok", "const char* append");
    out.clear();
    html_escape_append_value(out, const_text);
    check(out == "ok", "const char* escape");
    check(!value_empty(const_text), "const char* nonempty");
    check(value_size(const_text) == 2, "const char* size");
  }

  if (failures == 0) {
    std::cout << "codegen no-simd helper: all checks passed\n";
    return 0;
  }
  std::cerr << "codegen no-simd helper: " << failures << " checks failed\n";
  return 1;
}
