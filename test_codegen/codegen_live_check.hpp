#pragma once
/** @file codegen_live_check.hpp
 *  @brief test_codegen_live 系ドライバ共通の比較ヘルパ
 */
#include <iostream>
#include <string>

#include <injamm.hpp>
#include "codegen_case_types.hpp"

inline int live_test_count = 0;
inline int live_pass_count = 0;

/** @brief VM と生成コードの出力を 2 つのデータセット（truthy/falsy 側）で比較する */
inline void live_check(std::string_view name, std::string_view tmpl_str, CaseData const& d, CaseData const& d2, auto gen_func) {
  for (int variant = 0; variant < 2; ++variant) {
    ++live_test_count;
    auto const&        data = (variant == 0) ? d : d2;
    std::string const  tag  = std::string{name} + ((variant == 0) ? "" : "/falsy");
    injamm::engine<CaseData> eng{std::string(tmpl_str)};
    auto expected = eng.render(data);
    if (!expected) {
      std::cerr << "FAIL [" << tag << "] VM render failed: " << expected.error().custom_error_message << "\n";
      continue;
    }
    auto result = gen_func(data);
    if (!result) {
      std::cerr << "FAIL [" << tag << "] generated render failed: " << result.error().custom_error_message << "\n";
      continue;
    }
    if (*expected == *result) {
      std::cout << "PASS [" << tag << "]\n";
      ++live_pass_count;
    } else {
      std::cerr << "FAIL [" << tag << "]\n  expected: [" << *expected << "]\n  got:      [" << *result << "]\n";
    }
  }
}

inline void live_generated_check(std::string_view name, std::string_view expected, CaseData const& data, auto gen_func) {
  ++live_test_count;
  auto result = gen_func(data);
  if (!result) {
    std::cerr << "FAIL [" << name << "] generated render failed: "
              << result.error().custom_error_message << "\n";
  } else if (*result == expected) {
    std::cout << "PASS [" << name << "]\n";
    ++live_pass_count;
  } else {
    std::cerr << "FAIL [" << name << "]\n  expected: [" << expected
              << "]\n  got:      [" << *result << "]\n";
  }
}
