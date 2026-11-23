/**
 * @file test_codegen_live_namespaces.cpp
 * @brief 異なる名前空間・プレフィックスの生成ヘッダを同一 TU で合成する
 */

#include "codegen_live_check.hpp"

#include "case_ns_alpha.hpp"
#include "case_ns_beta.hpp"
#include "case_ns_guard_alpha.hpp"
#include "case_ns_guard_beta.hpp"
#include "case_ns_case_lower.hpp"
#include "case_ns_case_upper.hpp"
#include "case_ns_tuple_a.hpp"
#include "case_ns_tuple_b.hpp"
#include "case_partial_shared_alpha.hpp"
#include "case_partial_shared_beta.hpp"

int run_namespace_composition_checks(CaseData const& data) {
  auto check = [](std::string_view name, auto render, std::string_view expected) {
    ++live_test_count;
    auto result = render();
    if (!result) {
      std::cerr << "FAIL [" << name << "] generated render failed: "
                << result.error().custom_error_message << "\n";
      return;
    }
    if (*result == expected) {
      std::cout << "PASS [" << name << "]\n";
      ++live_pass_count;
    } else {
      std::cerr << "FAIL [" << name << "] expected: [" << expected
                << "] got: [" << *result << "]\n";
    }
  };

  check("distinct generated namespaces alpha",
        [&] { return codegen_ns_alpha::case_ns_alpha(data); }, data.name);
  check("distinct generated namespaces beta",
        [&] { return codegen_ns_beta::case_ns_beta(data); }, data.name);
  check("same prefix in distinct namespaces alpha",
        [&] { return codegen_ns_guard_alpha::case_ns_guard(data); }, data.name);
  check("same prefix in distinct namespaces beta",
        [&] { return codegen_ns_guard_beta::case_ns_guard(data); }, data.name);
  check("case-sensitive generated namespace lower",
        [&] { return codegen_ns_case::case_ns_case(data); }, data.name);
  check("case-sensitive generated namespace upper",
        [&] { return CODEGEN_NS_CASE::case_ns_case(data); }, data.name);
  check("namespace/prefix tuple a_b,c",
        [&] { return a_b::c(data); }, data.name);
  check("namespace/prefix tuple a,b_c",
        [&] { return a::b_c(data); }, data.name);
  check("same namespace partial prefix alpha",
        [&] { return codegen_shared_partial::codegen_partial_shared_alpha(data); }, "A:R");
  check("same namespace partial prefix beta",
        [&] { return codegen_shared_partial::codegen_partial_shared_beta(data); }, "B:R");

  auto check_partial = [](std::string_view name, auto render, std::string_view expected) {
    ++live_test_count;
    std::string out;
    auto result = render(out);
    if (!result) {
      std::cerr << "FAIL [" << name << "] generated partial render failed: "
                << result.error().custom_error_message << "\n";
      return;
    }
    if (out == expected) {
      std::cout << "PASS [" << name << "]\n";
      ++live_pass_count;
    } else {
      std::cerr << "FAIL [" << name << "] expected: [" << expected
                << "] got: [" << out << "]\n";
    }
  };

  check_partial("same namespace public partial alpha",
                [&](auto& out) { return codegen_shared_partial::render_partial<"alpha">(data, out); }, "X:R");
  check_partial("same namespace public partial beta",
                [&](auto& out) { return codegen_shared_partial::render_partial<"beta">(data, out); }, "Y:R");
  check_partial("same namespace prefixed partial alpha",
                [&](auto& out) { return codegen_shared_partial::codegen_partial_shared_alpha_partial<"shared">(data, out); }, "A:R");
  check_partial("same namespace prefixed partial beta",
                [&](auto& out) { return codegen_shared_partial::codegen_partial_shared_beta_partial<"shared">(data, out); }, "B:R");

  ++live_test_count;
  std::string unknown_out;
  auto unknown = codegen_shared_partial::render_partial<"missing">(data, unknown_out);
  if (!unknown && unknown.error().ec == injamm::error_code::unknown_key) {
    std::cout << "PASS [same namespace unknown partial]\n";
    ++live_pass_count;
  } else {
    std::cerr << "FAIL [same namespace unknown partial]\n";
  }
  return 0;
}
