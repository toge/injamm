#include "injamm/bytecode_io.hpp"
#include "injamm/filters.hpp"
#include "injamm/serialize_value.hpp"
#include "injamm/types.hpp"
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

TEST_CASE("new error codes have messages", "[noexcept]") {
  CHECK(injamm::error_code_to_message(injamm::error_code::out_of_memory) == "Out of memory");
  CHECK(injamm::error_code_to_message(injamm::error_code::invalid_format) == "Invalid format string");
}

TEST_CASE("try_append and try_push_back succeed", "[noexcept]") {
  injamm::error_ctx err;
  std::string s;
  CHECK(injamm::detail::try_append(s, std::string_view{"abc"}, &err));
  CHECK(s == "abc");
  CHECK(err.ec == injamm::error_code::none);

  std::vector<int> v;
  CHECK(injamm::detail::try_push_back(v, 42, &err));
  REQUIRE(v.size() == 1);
  CHECK(v[0] == 42);
  CHECK(err.ec == injamm::error_code::none);
}

TEST_CASE("string/float filters are fallible and noexcept", "[noexcept]") {
  std::string s = "abc";
  injamm::detail::string_filter_entry se{injamm::detail::string_filter::upper};
  auto r = injamm::detail::apply_string_filter(s, se);
  CHECK(r);
  CHECK(s == "ABC");

  std::string f = "3.14159";
  injamm::detail::float_filter_entry fe{injamm::detail::float_filter::precision, 2};
  auto rf = injamm::detail::apply_float_filter(f, fe);
  CHECK(rf);
  CHECK(f == "3.14");

  static_assert(noexcept(injamm::detail::apply_string_filter(s, se)));
  static_assert(noexcept(injamm::detail::apply_float_filter(f, fe)));
}

TEST_CASE("serialize_formatted maps format_error to invalid_format", "[noexcept]") {
  std::string out;
  auto r = injamm::detail::serialize_formatted(out, 42, "{invalid!");
  CHECK(!r);
  CHECK(r.error().ec == injamm::error_code::invalid_format);
}
