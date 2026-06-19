#pragma once

#include <injamm/bytecode.hpp>
#include <injamm/sqlite3/concept.hpp>
#include <injamm/sqlite3/executor.hpp>
#include <injamm/bytecode_compile.hpp>
#include <expected>
#include <string>
#include <string_view>

namespace injamm::sqlite3 {

template <class T>
  requires runtime_field_accessible<T> || (forward_iterable<T> && runtime_field_accessible<typename T::value_type>)
class runtime_engine {
  injamm::detail::bytecode bc_;

public:
  explicit runtime_engine(std::string_view tmpl, bool trim_blocks = false, bool lstrip_blocks = false)
    : bc_(injamm::detail::bc_compile<T>(tmpl, trim_blocks, lstrip_blocks)) {}

  [[nodiscard]] std::expected<std::string, injamm::error_ctx> render(T const& value) const {
    return detail::bc_execute(bc_, value);
  }

  [[nodiscard]] std::expected<void, injamm::error_ctx> render(T const& value, std::string& out) const {
    return detail::bc_execute_into(bc_, value, out);
  }
};

} // namespace injamm::sqlite3
