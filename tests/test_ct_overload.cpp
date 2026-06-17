#include "injamm.hpp"
#include <catch2/catch_test_macros.hpp>
#include <glaze/glaze.hpp>

struct CtUser {
  std::string name;
  int age{};
};

template <>
struct glz::meta<CtUser> {
  static constexpr auto value = glz::object("name", &CtUser::name, "age", &CtUser::age);
};

TEST_CASE("ct overload test", "[ct][trim_blocks]") {
  CtUser user{"Alice", 30};
  
  // This should work - explicitly passing bool flags
  SECTION("explicit false, false") {
    auto r = injamm::render<"{{name}}{{age}}", false, false>(user);
    REQUIRE(r.has_value());
    CHECK(*r == "Alice30");
  }
  
  SECTION("explicit true, false") {
    auto r = injamm::render<"{{name}}{{age}}", true, false>(user);
    REQUIRE(r.has_value());
    CHECK(*r == "Alice30");
  }
  
  SECTION("without flags") {
    auto r = injamm::render<"{{name}}{{age}}">(user);
    REQUIRE(r.has_value());
    CHECK(*r == "Alice30");
  }
}
