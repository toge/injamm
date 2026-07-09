#pragma once

/**
 * @file bytecode_debug.hpp
 * @brief バイトコードのデバッグ支援関数（disassemble, opcode名前解決）
 * @details プロダクションHeaderCodeからは分離。テストやデバッグ時のみincludeする。
 */

#include "bytecode.hpp"
#include <charconv>
#include <string>

namespace injamm::detail {

[[nodiscard]] inline std::string_view opcode_name(bc_opcode op) noexcept {
  switch (op) {
  case bc_opcode::emit_literal:            return "emit_literal";
  case bc_opcode::emit_var:                return "emit_var";
  case bc_opcode::emit_var_raw:            return "emit_var_raw";
  case bc_opcode::emit_section:            return "emit_section";
  case bc_opcode::emit_end:                return "emit_end";
  case bc_opcode::emit_inverted:           return "emit_inverted";
  case bc_opcode::emit_at_index:           return "emit_at_index";
  case bc_opcode::emit_at_first:           return "emit_at_first";
  case bc_opcode::emit_at_last:            return "emit_at_last";
  case bc_opcode::emit_if:                 return "emit_if";
  case bc_opcode::emit_if_eq:              return "emit_if_eq";
  case bc_opcode::emit_if_ne:              return "emit_if_ne";
  case bc_opcode::emit_if_gt:              return "emit_if_gt";
  case bc_opcode::emit_if_gte:             return "emit_if_gte";
  case bc_opcode::emit_if_lt:              return "emit_if_lt";
  case bc_opcode::emit_if_lte:             return "emit_if_lte";
  case bc_opcode::emit_else:               return "emit_else";
  case bc_opcode::emit_endif:              return "emit_endif";
  case bc_opcode::emit_at_section:         return "emit_at_section";
  case bc_opcode::emit_at_inverted:        return "emit_at_inverted";
  case bc_opcode::emit_litvar:             return "emit_litvar";
  case bc_opcode::emit_litvar_raw:         return "emit_litvar_raw";
  case bc_opcode::emit_at_root:            return "emit_at_root";
  case bc_opcode::emit_at_root_field:      return "emit_at_root_field";
  case bc_opcode::emit_at_root_field_raw:  return "emit_at_root_field_raw";
  case bc_opcode::emit_at_key:             return "emit_at_key";
  case bc_opcode::emit_this:               return "emit_this";
  case bc_opcode::resolve_filtered:        return "resolve_filtered";
  case bc_opcode::filter_upper:            return "filter_upper";
  case bc_opcode::filter_lower:            return "filter_lower";
  case bc_opcode::filter_capitalize:       return "filter_capitalize";
  case bc_opcode::filter_title:            return "filter_title";
  case bc_opcode::filter_trim:             return "filter_trim";
  case bc_opcode::filter_ltrim:            return "filter_ltrim";
  case bc_opcode::filter_rtrim:            return "filter_rtrim";
  case bc_opcode::filter_left:             return "filter_left";
  case bc_opcode::filter_right:            return "filter_right";
  case bc_opcode::filter_center:           return "filter_center";
  case bc_opcode::filter_truncate:         return "filter_truncate";
  case bc_opcode::filter_substr:           return "filter_substr";
  case bc_opcode::filter_replace:          return "filter_replace";
  case bc_opcode::filter_default:          return "filter_default";
  case bc_opcode::filter_json:             return "filter_json";
  case bc_opcode::filter_safe:             return "filter_safe";
  case bc_opcode::filter_indent:           return "filter_indent";
  case bc_opcode::filter_pad:              return "filter_pad";
  case bc_opcode::filter_pluralize:        return "filter_pluralize";
  case bc_opcode::filter_format:           return "filter_format";
  case bc_opcode::emit_filtered:           return "emit_filtered";
  case bc_opcode::emit_filtered_raw:       return "emit_filtered_raw";
  case bc_opcode::filter_int_abs:          return "filter_int_abs";
  case bc_opcode::filter_int_hex:          return "filter_int_hex";
  case bc_opcode::filter_int_oct:          return "filter_int_oct";
  case bc_opcode::filter_int_bin:          return "filter_int_bin";
  case bc_opcode::filter_int_neg:          return "filter_int_neg";
  case bc_opcode::filter_int_mod:          return "filter_int_mod";
  case bc_opcode::filter_int_numify:       return "filter_int_numify";
  case bc_opcode::filter_int_is_neg:       return "filter_int_is_neg";
  case bc_opcode::filter_int_eq:           return "filter_int_eq";
  case bc_opcode::filter_int_ne:           return "filter_int_ne";
  case bc_opcode::filter_int_gt:           return "filter_int_gt";
  case bc_opcode::filter_int_gte:          return "filter_int_gte";
  case bc_opcode::filter_int_lt:           return "filter_int_lt";
  case bc_opcode::filter_int_lte:          return "filter_int_lte";
  case bc_opcode::filter_int_zerofill:     return "filter_int_zerofill";
  case bc_opcode::filter_int_add:          return "filter_int_add";
  case bc_opcode::filter_int_sub:          return "filter_int_sub";
  case bc_opcode::filter_int_mul:          return "filter_int_mul";
  case bc_opcode::filter_int_div:          return "filter_int_div";
  case bc_opcode::filter_float_precision:  return "filter_float_precision";
  case bc_opcode::emit_if_filtered:        return "emit_if_filtered";
  case bc_opcode::emit_break:              return "emit_break";
  case bc_opcode::emit_continue:           return "emit_continue";
  case bc_opcode::emit_at_index1:          return "emit_at_index1";
  case bc_opcode::emit_at_size:            return "emit_at_size";
  case bc_opcode::emit_var_size:           return "emit_var_size";
  case bc_opcode::emit_if_or:              return "emit_if_or";
  case bc_opcode::emit_if_and:             return "emit_if_and";
  case bc_opcode::emit_if_not:             return "emit_if_not";
  case bc_opcode::call_partial:            return "call_partial";
  case bc_opcode::halt:                    return "halt";
  }
  return "unknown";
}

[[nodiscard]] inline std::string_view string_filter_name(string_filter f) noexcept {
  switch (f) {
  case string_filter::upper:      return "upper";
  case string_filter::lower:      return "lower";
  case string_filter::capitalize: return "capitalize";
  case string_filter::title:      return "title";
  case string_filter::trim:       return "trim";
  case string_filter::ltrim:      return "ltrim";
  case string_filter::rtrim:      return "rtrim";
  case string_filter::left:       return "left";
  case string_filter::right:      return "right";
  case string_filter::center:     return "center";
  case string_filter::truncate:   return "truncate";
  case string_filter::substr:     return "substr";
  case string_filter::replace:    return "replace";
  case string_filter::default_value: return "default";
  case string_filter::to_json:    return "json";
  case string_filter::safe:       return "safe";
  case string_filter::indent:     return "indent";
  case string_filter::pad:        return "pad";
  case string_filter::pluralize:  return "pluralize";
  case string_filter::format:     return "format";
  }
  return "unknown";
}

[[nodiscard]] inline std::string_view int_filter_name(int_filter f) noexcept {
  switch (f) {
  case int_filter::abs:     return "abs";
  case int_filter::hex:     return "hex";
  case int_filter::oct:     return "oct";
  case int_filter::bin:     return "bin";
  case int_filter::neg:     return "neg";
  case int_filter::mod:     return "mod";
  case int_filter::numify:  return "numify";
  case int_filter::is_neg:  return "is_neg";
  case int_filter::eq:      return "eq";
  case int_filter::ne:      return "ne";
  case int_filter::gt:      return "gt";
  case int_filter::gte:     return "gte";
  case int_filter::lt:      return "lt";
  case int_filter::lte:     return "lte";
  case int_filter::zerofill: return "zerofill";
  case int_filter::add:     return "add";
  case int_filter::sub:     return "sub";
  case int_filter::mul:     return "mul";
  case int_filter::div:     return "div";
  }
  return "unknown";
}

[[nodiscard]] inline std::string_view float_filter_name(float_filter f) noexcept {
  switch (f) {
  case float_filter::precision: return "precision";
  }
  return "unknown";
}

inline std::string bytecode::disassemble() const {
  std::string out;
  auto append = [&](std::string_view sv) {
    out.append(sv);
  };

  // 命令列
  append("--- instructions ---\n");
  for (std::size_t i = 0; i < instructions.size(); ++i) {
    auto const& instr = instructions[i];
    auto addr = static_cast<std::uint32_t>(i);

    // アドレス
    char addr_buf[16];
    auto [p1, ec1] = std::to_chars(addr_buf, addr_buf + sizeof(addr_buf), addr);
    append(std::string_view{addr_buf, static_cast<std::size_t>(p1 - addr_buf)});
    append("  ");

    // オペコード名
    auto const oname = opcode_name(instr.op);
    append(oname);
    long long const pad = 22 - static_cast<long long>(p1 - addr_buf) - static_cast<long long>(oname.size());
    for (long long j = 0; j < pad; ++j) {
      out.push_back(' ');
    }

    // オペランド
    switch (instr.op) {
    case bc_opcode::emit_literal: {
      if (instr.operand < literals.size()) {
        append("\"");
        append(literals[instr.operand]);
        append("\"");
      } else {
        char buf[16];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
        append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
      }
      break;
    }
    case bc_opcode::emit_var:
    case bc_opcode::emit_var_raw:
    case bc_opcode::emit_section:
    case bc_opcode::emit_inverted:
    case bc_opcode::emit_at_index:
    case bc_opcode::emit_at_first:
    case bc_opcode::emit_at_last:
    case bc_opcode::emit_at_section:
    case bc_opcode::emit_at_inverted:
    case bc_opcode::emit_at_key:
    case bc_opcode::emit_this:
    case bc_opcode::emit_break:
    case bc_opcode::emit_continue: {
      if (instr.operand2 < var_refs.size()) {
        append(var_refs[instr.operand2].key);
        if (instr.op == bc_opcode::emit_section || instr.op == bc_opcode::emit_inverted || instr.op == bc_opcode::emit_at_section || instr.op == bc_opcode::emit_at_inverted) {
          append("  -> ");
          char buf[16];
          auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
          append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
          if (instr.operand3 != 0 && (instr.op == bc_opcode::emit_section || instr.op == bc_opcode::emit_inverted)) {
            append("  else_target=");
            auto [p2, ec2] = std::to_chars(buf, buf + sizeof(buf), instr.operand3);
            append(std::string_view{buf, static_cast<std::size_t>(p2 - buf)});
          }
        } else {
          append("  loop#");
          char buf[16];
          auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand2);
          append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
        }
        break;
      } else {
        char buf[16];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand2);
        append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
      }
      break;
    }
    case bc_opcode::emit_at_index1: {
      char buf[16];
      auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand2);
      append("@index1 (loop#");
      append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
      append(")");
      break;
    }
    case bc_opcode::emit_litvar:
    case bc_opcode::emit_litvar_raw: {
      if (instr.operand < literals.size()) {
        append("\"");
        append(literals[instr.operand]);
        append("\" ");
      }
      if (instr.operand2 < var_refs.size()) {
        append("+ ");
        append(var_refs[instr.operand2].key);
      }
      break;
    }
    case bc_opcode::emit_if:
    case bc_opcode::emit_else:
    case bc_opcode::emit_endif: {
      if (instr.operand2 < var_refs.size()) {
        append(var_refs[instr.operand2].key);
        append("  -> ");
      }
      char buf[16];
      auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
      append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
      break;
    }
    case bc_opcode::emit_end: {
      append("-> ");
      char buf[16];
      auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
      append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
      break;
    }
    case bc_opcode::emit_at_root:
    case bc_opcode::emit_at_root_field:
    case bc_opcode::emit_at_root_field_raw: {
      if (instr.operand2 < var_refs.size()) {
        append(var_refs[instr.operand2].key);
      }
      break;
    }
    case bc_opcode::resolve_filtered:
    case bc_opcode::emit_filtered:
    case bc_opcode::emit_filtered_raw:
    case bc_opcode::emit_if_filtered: {
      if (instr.operand2 < var_refs.size()) {
        append(var_refs[instr.operand2].key);
      }
      break;
    }
    case bc_opcode::filter_left:
    case bc_opcode::filter_right:
    case bc_opcode::filter_center:
    case bc_opcode::filter_truncate:
    case bc_opcode::filter_int_mod:
    case bc_opcode::filter_int_add:
    case bc_opcode::filter_int_sub:
    case bc_opcode::filter_int_mul:
    case bc_opcode::filter_int_div: {
      char buf[16];
      auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
      append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
      break;
    }
    case bc_opcode::filter_substr: {
      append("start=");
      char buf[16];
      auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
      append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
      append(" len=");
      auto [p2, ec2] = std::to_chars(buf, buf + sizeof(buf), instr.operand2);
      append(std::string_view{buf, static_cast<std::size_t>(p2 - buf)});
      break;
    }
    case bc_opcode::call_partial: {
      if (instr.operand < partial_entries.size()) {
        append("partial=\"");
        append(partial_entries[instr.operand].name);
        append("\"");
      } else {
        char buf[16];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
        append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
      }
      break;
    }
    case bc_opcode::filter_default: {
      if (instr.operand < literals.size()) {
        append("\"");
        append(literals[instr.operand]);
        append("\"");
      }
      break;
    }
    case bc_opcode::filter_pad: {
      char buf[16];
      auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
      append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
      if (instr.operand2 < literals.size()) {
        append(" \"");
        append(literals[instr.operand2]);
        append("\"");
      }
      break;
    }
    case bc_opcode::filter_pluralize: {
      if (instr.operand < literals.size()) {
        append("\"");
        append(literals[instr.operand]);
        append("\"");
      }
      if (instr.operand2 < literals.size()) {
        append(" \"");
        append(literals[instr.operand2]);
        append("\"");
      }
      break;
    }
    case bc_opcode::filter_float_precision:
    case bc_opcode::filter_int_eq:
    case bc_opcode::filter_int_ne:
    case bc_opcode::filter_int_gt:
    case bc_opcode::filter_int_gte:
    case bc_opcode::filter_int_lt:
    case bc_opcode::filter_int_lte:
    case bc_opcode::filter_int_zerofill: {
      char buf[16];
      auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), instr.operand);
      append(std::string_view{buf, static_cast<std::size_t>(p - buf)});
      break;
    }
    default:
      break;
    }
    append("\n");
  }

  // リテラルテーブル
  if (!literals.empty()) {
    append("\n--- literals ---\n");
    for (std::size_t i = 0; i < literals.size(); ++i) {
      char addr_buf[16];
      auto [p, ec] = std::to_chars(addr_buf, addr_buf + sizeof(addr_buf), i);
      append(std::string_view{addr_buf, static_cast<std::size_t>(p - addr_buf)});
      append(": \"");
      append(literals[i]);
      append("\"\n");
    }
  }

  // 変数参照テーブル
  if (!var_refs.empty()) {
    append("\n--- var_refs ---\n");
    for (std::size_t i = 0; i < var_refs.size(); ++i) {
      char addr_buf[16];
      auto [p, ec] = std::to_chars(addr_buf, addr_buf + sizeof(addr_buf), i);
      append(std::string_view{addr_buf, static_cast<std::size_t>(p - addr_buf)});
      append(": ");
      append(var_refs[i].key);
      if (var_refs[i].field_index != UINT32_MAX) {
        append("  field=");
        char fbuf[16];
        auto [fp, fec] = std::to_chars(fbuf, fbuf + sizeof(fbuf), var_refs[i].field_index);
        append(std::string_view{fbuf, static_cast<std::size_t>(fp - fbuf)});
      }
      if (!var_refs[i].filters.empty()) {
        append("  filters=[");
        for (std::size_t j = 0; j < var_refs[i].filters.size(); ++j) {
          if (j > 0) {
            append(", ");
          }
          append(string_filter_name(var_refs[i].filters[j].filter));
        }
        append("]");
      }
      if (!var_refs[i].int_filters.empty()) {
        append("  int_filters=[");
        for (std::size_t j = 0; j < var_refs[i].int_filters.size(); ++j) {
          if (j > 0) {
            append(", ");
          }
          append(int_filter_name(var_refs[i].int_filters[j].filter));
        }
        append("]");
      }
      if (!var_refs[i].float_filters.empty()) {
        append("  float_filters=[");
        for (std::size_t j = 0; j < var_refs[i].float_filters.size(); ++j) {
          if (j > 0) {
            append(", ");
          }
          append(float_filter_name(var_refs[i].float_filters[j].filter));
        }
        append("]");
      }
      append("\n");
    }
  }

  // partial テーブル
  if (!partial_entries.empty()) {
    append("\n--- partials ---\n");
    for (std::size_t i = 0; i < partial_entries.size(); ++i) {
      char addr_buf[16];
      auto [p, ec] = std::to_chars(addr_buf, addr_buf + sizeof(addr_buf), i);
      append(std::string_view{addr_buf, static_cast<std::size_t>(p - addr_buf)});
      append(": \"");
      append(partial_entries[i].name);
      append("\" (");
      auto [p2, ec2] = std::to_chars(addr_buf, addr_buf + sizeof(addr_buf), partial_entries[i].bc->instructions.size());
      append(std::string_view{addr_buf, static_cast<std::size_t>(p2 - addr_buf)});
      append(" instr)\n");
    }
  }

  return out;
}

} // namespace injamm::detail
