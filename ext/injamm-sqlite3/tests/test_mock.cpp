#include <catch2/catch_test_macros.hpp>
#include <injamm/sqlite3/concept.hpp>
#include <map>
#include <string>

using namespace injamm::sqlite3;

struct mock_row {
  std::string find(std::string_view key) const {
    auto it = data_.find(std::string(key));
    return it != data_.end() ? it->second : "";
  }
  std::map<std::string, std::string> data_;
};

struct mock_result {
  mock_row*   rows_;
  std::size_t count_;

  struct iterator {
    mock_row* ptr_;
    mock_row* last_;
    auto&     operator++() {
      ++ptr_;
      return *this;
    }
    auto& operator*() { return *ptr_; }
    bool  operator!=(std::nullptr_t) const { return ptr_ < last_; }
  };

  using value_type = mock_row;
  iterator       begin() const { return iterator{rows_, rows_ + count_}; }
  std::nullptr_t end() const { return nullptr; }
};

TEST_CASE("concepts satisfy", "[concept]") {
  SECTION("mock_row is runtime_field_accessible") {
    static_assert(runtime_field_accessible<mock_row>);
  }
  SECTION("mock_result is forward_iterable") {
    static_assert(forward_iterable<mock_result>);
  }
  SECTION("mock_row find works") {
    mock_row r{{{"name", "Alice"}}};
    CHECK(r.find("name") == "Alice");
    CHECK(r.find("nonexistent") == "");
  }
}

#include <injamm/sqlite3/engine.hpp>

TEST_CASE("mock rendering", "[engine]") {
  SECTION("single row, single var") {
    mock_row r{{{"name", "Alice"}, {"email", "alice@example.com"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("Hello {{name}} ({{email}})");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "Hello Alice (alice@example.com)");
  }

  SECTION("unknown key renders empty") {
    mock_row r{{{"name", "Bob"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("Hi {{name}}, age={{age}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "Hi Bob, age=");
  }

  SECTION("{{{raw}}} variable output") {
    mock_row r{{{"html", "<b>bold</b>"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{html}} vs {{{html}}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "&lt;b&gt;bold&lt;/b&gt; vs <b>bold</b>");
  }

  SECTION("string filter works") {
    mock_row r{{{"name", "alice"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{name | upper}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "ALICE");
  }

  SECTION("string truthiness works in if") {
    mock_row r{{{"status", "Pending"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{#if status}}YES{{else}}NO{{/if}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "YES");
  }

  SECTION("empty string is falsy in if") {
    mock_row r{{{"status", ""}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{#if status}}YES{{else}}NO{{/if}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "NO");
  }

  SECTION("enum-like string equality works in if") {
    mock_row r{{{"status", "Pending"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{#if status == \"Pending\"}}YES{{else}}NO{{/if}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "YES");
  }

  SECTION("enum-like string inequality works in if") {
    mock_row r{{{"status", "Pending"}}};
    auto     eng    = injamm::sqlite3::runtime_engine<mock_row>("{{#if status != \"Active\"}}YES{{else}}NO{{/if}}");
    auto     result = eng.render(r);
    REQUIRE(result.has_value());
    CHECK(*result == "YES");
  }
}

TEST_CASE("forward iteration rendering", "[engine][iteration]") {
  SECTION("two rows with {{#.}}") {
    mock_row    rows_arr[2] = {mock_row{{{"name", "Alice"}}}, mock_row{{{"name", "Bob"}}}};
    mock_result res{rows_arr, 2};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("<ul>{{#.}}<li>{{name}}</li>{{/.}}</ul>");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "<ul><li>Alice</li><li>Bob</li></ul>");
  }

  SECTION("{{name}} without literal prefix (no fusion)") {
    mock_row    rows_arr[2] = {mock_row{{{"name", "Alice"}}}, mock_row{{{"name", "Bob"}}}};
    mock_result res{rows_arr, 2};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("{{#.}}{{name}} {{/.}}");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "Alice Bob ");
  }

  SECTION("loop.index works") {
    mock_row    rows_arr[2] = {mock_row{{{"name", "A"}}}, mock_row{{{"name", "B"}}}};
    mock_result res{rows_arr, 2};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("{{#.}}{{loop.index}}:{{name}} {{/.}}");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "0:A 1:B ");
  }

  SECTION("empty result renders nothing") {
    mock_result res{nullptr, 0};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("before{{#.}}middle{{/.}}after");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "beforeafter");
  }

  SECTION("{{#.}} with else, empty") {
    mock_result res{nullptr, 0};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("{{#.}}body{{else}}empty{{/.}}");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "empty");
  }

  SECTION("{{#.}} with else, non-empty") {
    mock_row    rows_arr[1] = {mock_row{{{"name", "A"}}}};
    mock_result res{rows_arr, 1};
    auto        eng    = injamm::sqlite3::runtime_engine<mock_result>("{{#.}}{{name}}{{else}}empty{{/.}}");
    auto        result = eng.render(res);
    REQUIRE(result.has_value());
    CHECK(*result == "A");
  }
}
