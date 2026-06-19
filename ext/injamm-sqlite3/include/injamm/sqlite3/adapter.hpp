#pragma once

#include <injamm/sqlite3/concept.hpp>
#include <sqlite3.h>
#include <charconv>
#include <string>
#include <string_view>

namespace injamm::sqlite3 {

struct sqlite3_row_view {
  sqlite3_stmt* stmt_;

  explicit sqlite3_row_view(sqlite3_stmt* stmt) : stmt_(stmt) {}

  std::string find(std::string_view key) const {
    int n = sqlite3_column_count(stmt_);
    for (int i = 0; i < n; ++i) {
      if (key == sqlite3_column_name(stmt_, i)) {
        auto t = sqlite3_column_type(stmt_, i);
        switch (t) {
          case SQLITE_INTEGER: {
            auto val = sqlite3_column_int64(stmt_, i);
            char buf[24];
            auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), val);
            return std::string(buf, static_cast<std::size_t>(p - buf));
          }
          case SQLITE_FLOAT: {
            auto val = sqlite3_column_double(stmt_, i);
            char buf[64];
            auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), val);
            return std::string(buf, static_cast<std::size_t>(p - buf));
          }
          case SQLITE_TEXT: {
            auto text = sqlite3_column_text(stmt_, i);
            auto len = sqlite3_column_bytes(stmt_, i);
            return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(len));
          }
          default:
            return "";
        }
      }
    }
    return "";
  }
};

struct sqlite3_result {
  sqlite3_stmt* stmt_;
  mutable bool started_;

  explicit sqlite3_result(sqlite3_stmt* stmt) : stmt_(stmt), started_(false) {}

  using value_type = sqlite3_row_view;

  struct sentinel {};
  struct iterator {
    sqlite3_stmt* stmt_;
    int rc_ = SQLITE_ROW;
    sqlite3_row_view current_;

    iterator& operator++() {
      rc_ = sqlite3_step(stmt_);
      current_ = sqlite3_row_view{stmt_};
      return *this;
    }
    sqlite3_row_view const& operator*() const { return current_; }
    bool operator!=(sentinel) const { return rc_ == SQLITE_ROW; }
  };

  iterator begin() const {
    if (!started_) {
      started_ = true;
      int rc = sqlite3_step(stmt_);
      return iterator{stmt_, rc, sqlite3_row_view{stmt_}};
    }
    return iterator{nullptr, SQLITE_DONE, sqlite3_row_view{nullptr}};
  }
  sentinel end() const { return {}; }
};

} // namespace injamm::sqlite3
