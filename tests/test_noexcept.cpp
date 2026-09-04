#include "injamm/bytecode_io.hpp"
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
