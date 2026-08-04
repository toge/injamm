#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace injamm {
struct error_ctx;
}
namespace injamm::detail {

enum class bc_opcode : std::uint8_t {
  emit_literal = 0,
  emit_var = 1,
  emit_var_raw = 2,
  emit_section = 3,
  emit_end = 4,
  emit_inverted = 5,
  emit_at_index = 6,
  emit_at_first = 7,
  emit_at_last = 8,
  emit_if = 9,
  emit_if_eq = 10,
  emit_if_ne = 11,
  emit_if_gt = 12,
  emit_if_gte = 13,
  emit_if_lt = 14,
  emit_if_lte = 15,
  emit_else = 16,
  emit_endif = 17,
  emit_at_section = 18,
  emit_at_inverted = 19,
  emit_litvar = 20,
  emit_litvar_raw = 21,
  emit_at_root = 22,
  emit_at_root_field = 23,
  emit_at_root_field_raw = 24,
  emit_at_key = 25,
  emit_this = 26,
  resolve_filtered = 27,
  filter_upper = 28,
  filter_lower = 29,
  filter_capitalize = 30,
  filter_title = 31,
  filter_trim = 32,
  filter_ltrim = 33,
  filter_rtrim = 34,
  filter_left = 35,
  filter_right = 36,
  filter_center = 37,
  filter_truncate = 38,
  filter_substr = 39,
  filter_replace = 40,
  filter_default = 41,
  filter_json = 42,
  filter_safe = 43,
  filter_indent = 44,
  filter_pad = 45,
  filter_pluralize = 46,
  filter_format = 47,
  filter_repeat = 48,
  emit_filtered = 49,
  emit_filtered_raw = 50,
  filter_int_abs = 51,
  filter_int_hex = 52,
  filter_int_oct = 53,
  filter_int_bin = 54,
  filter_int_neg = 55,
  filter_int_mod = 56,
  filter_int_numify = 57,
  filter_int_is_neg = 58,
  filter_int_eq = 59,
  filter_int_ne = 60,
  filter_int_gt = 61,
  filter_int_gte = 62,
  filter_int_lt = 63,
  filter_int_lte = 64,
  filter_int_zerofill = 65,
  filter_int_add = 66,
  filter_int_sub = 67,
  filter_int_mul = 68,
  filter_int_div = 69,
  filter_float_precision = 70,
  emit_if_filtered = 71,
  emit_break = 72,
  emit_continue = 73,
  emit_at_index1 = 74,
  emit_at_size = 75,
  emit_var_size = 76,
  emit_if_or = 77,
  emit_if_and = 78,
  emit_if_not = 79,
  call_partial = 80,
  halt = 81
};

enum class string_filter : std::uint8_t {
  upper,
  lower,
  capitalize,
  title,
  trim,
  ltrim,
  rtrim,
  left,
  right,
  center,
  truncate,
  substr,
  replace,
  default_value,
  to_json,
  safe,
  indent,
  pad,
  pluralize,
  format,
  repeat
};

enum class int_filter : std::uint8_t {
  abs,
  hex,
  oct,
  bin,
  neg,
  mod,
  numify,
  is_neg,
  eq,
  ne,
  gt,
  gte,
  lt,
  lte,
  zerofill,
  add,
  sub,
  mul,
  div
};

enum class float_filter : std::uint8_t {
  precision
};

enum class compare_operand_kind : std::uint8_t {
  none,
  int_literal,
  string_literal,
  variable
};

struct bytecode;

template <class T>
std::expected<std::string, error_ctx> bc_execute(bytecode const& bc, T const& value, std::size_t size_hint = 0);

template <class T>
std::expected<void, error_ctx> bc_execute_into(bytecode const& bc, T const& value, std::string& out);

} // namespace injamm::detail
