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
  /** @brief 汎用フィルタ命令（operand3 に string_filter / int_filter / float_filter 種別） */
  filter_string = 28,
  filter_int = 29,
  filter_float = 30,
  emit_filtered = 31,
  emit_filtered_raw = 32,
  emit_if_filtered = 33,
  emit_break = 34,
  emit_continue = 35,
  emit_at_index1 = 36,
  emit_at_size = 37,
  emit_var_size = 38,
  emit_if_or = 39,
  emit_if_and = 40,
  emit_if_not = 41,
  call_partial = 42,
  halt = 43
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

/** @brief セクションフィルタオペコード種別 */
enum class section_filter_op_kind : std::uint8_t {
  reverse,     /**< 反転反復 */
  take,        /**< 先頭 n 個を残す */
  skip,        /**< 先頭 n 個を捨てる */
  take_last,   /**< 末尾 n 個を残す */
  skip_last    /**< 末尾 n 個を捨てる */
};

struct bytecode;

template <class T>
std::expected<std::string, error_ctx> bc_execute(bytecode const& bc, T const& value, std::size_t size_hint = 0);

template <class T>
std::expected<void, error_ctx> bc_execute_into(bytecode const& bc, T const& value, std::string& out);

template <class T, class Sink>
std::expected<void, error_ctx> bc_execute_into_sink(bytecode const& bc, T const& value, Sink& sink);

} // namespace injamm::detail
