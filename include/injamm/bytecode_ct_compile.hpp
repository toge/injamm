#pragma once

#include "ct_chunk.hpp"
#include "glz_dispatch.hpp"
#include "parse.hpp"
#include "types.hpp"
#include "bytecode.hpp"
#include "bytecode_exec.hpp"
// enum_io.hpp provides enum_name_to_int and serialize_enum

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace injamm::detail {

struct string_ref {
  const char* data = nullptr;
  std::size_t size = 0;
  constexpr string_ref() = default;
  constexpr string_ref(const char* d, std::size_t s) : data(d), size(s) {}
};

struct ct_var_ref {
  string_ref key;
  std::uint32_t field_index = UINT32_MAX;
  /** @brief 比較演算子の RHS 整数値（emit_if_eq/ne 等で使用） */
  int compare_rhs = 0;
  /** @brief compare_rhs が有効かどうか */
  bool has_compare_rhs = false;
};

struct ct_lit_entry {
  std::size_t offset = 0;
  std::size_t size = 0;
};

template <std::size_t N>
struct ct_bytecode {
  std::array<bc_instruction, N> instructions{};
  std::array<ct_lit_entry, N> lit_entries{};
  std::array<ct_var_ref, N> var_refs{};
  std::size_t instr_count{};
  std::size_t literal_count{};
  std::size_t var_ref_count{};
  std::array<char, N * 4> string_pool{};
  std::size_t string_pool_size{};
  error_ctx error{};
};

template <std::size_t N>
struct ct_bytecode_builder {
  ct_bytecode<N>& bc;

  constexpr std::uint32_t add_literal(string_ref lit) {
    auto idx = static_cast<std::uint32_t>(bc.literal_count);
    auto& entry = bc.lit_entries[bc.literal_count];
    entry.offset = bc.string_pool_size;
    entry.size = lit.size;
    auto* dest = bc.string_pool.data() + bc.string_pool_size;
    for (std::size_t i = 0; i < lit.size; ++i)
      dest[i] = lit.data[i];
    bc.string_pool_size += lit.size;
    ++bc.literal_count;
    return idx;
  }

  constexpr std::uint32_t add_var_ref(string_ref key, std::uint32_t field_index) {
    auto idx = static_cast<std::uint32_t>(bc.var_ref_count);
    bc.var_refs[bc.var_ref_count] = {key, field_index};
    ++bc.var_ref_count;
    return idx;
  }

  /** @brief 比較 RHS 整数値を保持する var_ref を追加する（emit_if_cmp 用） */
  constexpr std::uint32_t add_var_ref_cmp(string_ref key, std::uint32_t field_index, int compare_rhs) {
    auto idx = static_cast<std::uint32_t>(bc.var_ref_count);
    bc.var_refs[bc.var_ref_count] = {key, field_index, compare_rhs, /*has_compare_rhs=*/true};
    ++bc.var_ref_count;
    return idx;
  }

  constexpr void emit(bc_opcode op, std::uint32_t operand = 0, std::uint32_t operand2 = 0) {
    bc.instructions[bc.instr_count] = {op, operand, operand2};
    ++bc.instr_count;
  }

  constexpr std::size_t current_offset() const { return bc.instr_count; }

  constexpr void patch_jump(std::size_t idx, std::uint32_t target) {
    bc.instructions[idx].operand = target;
  }
};

// to_bytecode: ct_bytecode<N> → runtime bytecode
template <std::size_t N>
bytecode to_bytecode(ct_bytecode<N> const& ct) {
  bytecode bc;
  bc.instructions.assign(ct.instructions.begin(), ct.instructions.begin() + ct.instr_count);
  bc.literals.reserve(ct.literal_count);
  for (std::size_t i = 0; i < ct.literal_count; ++i)
    bc.literals.emplace_back(ct.string_pool.data() + ct.lit_entries[i].offset, ct.lit_entries[i].size);
  bc.var_refs.reserve(ct.var_ref_count);
  for (std::size_t i = 0; i < ct.var_ref_count; ++i) {
    bc_var_ref ref;
    ref.key.assign(ct.var_refs[i].key.data, ct.var_refs[i].key.size);
    ref.field_index = ct.var_refs[i].field_index;
    ref.has_dot = (ref.key.find('.') != std::string::npos);
    /** 比較演算子の RHS 整数値が設定されている場合は int_filters に追加（emit_if_cmp 用） */
    if (ct.var_refs[i].has_compare_rhs) {
      ref.int_filters.push_back({int_filter::eq, ct.var_refs[i].compare_rhs});
    }
    bc.var_refs.push_back(std::move(ref));
  }
  // Compute total literal size for buffer pre-allocation
  for (auto const& lit : bc.literals)
    bc.literal_total_size += lit.size();
  // Reconstruct filter chains from instruction stream (CT path doesn't store filters in var_refs)
  for (std::size_t i = 0; i < bc.instructions.size(); ++i) {
    if (bc.instructions[i].op == bc_opcode::resolve_filtered) {
      auto var_idx = bc.instructions[i].operand2;
      auto& ref = bc.var_refs[var_idx];
      std::size_t j = i + 1;
      while (j < bc.instructions.size()) {
        auto op = bc.instructions[j].op;
        auto const& fi = bc.instructions[j];
        if (op >= bc_opcode::filter_upper && op <= bc_opcode::filter_pluralize) {
          switch (op) {
          case bc_opcode::filter_upper:      ref.filters.push_back({.filter = string_filter::upper}); break;
          case bc_opcode::filter_lower:      ref.filters.push_back({.filter = string_filter::lower}); break;
          case bc_opcode::filter_capitalize: ref.filters.push_back({.filter = string_filter::capitalize}); break;
          case bc_opcode::filter_title:      ref.filters.push_back({.filter = string_filter::title}); break;
          case bc_opcode::filter_trim:       ref.filters.push_back({.filter = string_filter::trim}); break;
          case bc_opcode::filter_ltrim:      ref.filters.push_back({.filter = string_filter::ltrim}); break;
          case bc_opcode::filter_rtrim:      ref.filters.push_back({.filter = string_filter::rtrim}); break;
          case bc_opcode::filter_left:       ref.filters.push_back({.filter = string_filter::left, .arg1 = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_right:      ref.filters.push_back({.filter = string_filter::right, .arg1 = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_center:     ref.filters.push_back({.filter = string_filter::center, .arg1 = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_truncate:   ref.filters.push_back({.filter = string_filter::truncate, .arg1 = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_substr:     ref.filters.push_back({.filter = string_filter::substr, .arg1 = static_cast<int>(fi.operand), .arg2 = static_cast<int>(fi.operand2)}); break;
          case bc_opcode::filter_replace: {
            if (fi.operand != fi.operand2) {
              ref.filters.push_back({.filter = string_filter::replace, .str_arg1 = bc.literals[fi.operand], .str_arg2 = bc.literals[fi.operand2]});
            } else {
              ref.filters.push_back({.filter = string_filter::replace});
            }
            break;
          }
          case bc_opcode::filter_default: {
            ref.filters.push_back({.filter = string_filter::default_value, .str_arg1 = bc.literals[fi.operand]});
            break;
          }
          case bc_opcode::filter_json: {
            ref.filters.push_back({.filter = string_filter::to_json});
            break;
          }
          case bc_opcode::filter_safe: {
            ref.filters.push_back({.filter = string_filter::safe});
            break;
          }
          case bc_opcode::filter_indent: {
            ref.filters.push_back({.filter = string_filter::indent, .arg1 = static_cast<int>(fi.operand)});
            break;
          }
          case bc_opcode::filter_pad: {
            auto pad_str = (fi.operand2 != UINT32_MAX) ? bc.literals[fi.operand2] : std::string_view{};
            ref.filters.push_back({.filter = string_filter::pad, .arg1 = static_cast<int>(fi.operand), .str_arg1 = pad_str});
            break;
          }
          case bc_opcode::filter_pluralize: {
            ref.filters.push_back({.filter = string_filter::pluralize, .str_arg1 = bc.literals[fi.operand], .str_arg2 = bc.literals[fi.operand2]});
            break;
          }
          default: break;
          }
          ++j;
        } else if (op >= bc_opcode::filter_int_abs && op <= bc_opcode::filter_float_precision) {
          switch (op) {
          case bc_opcode::filter_int_abs:    ref.int_filters.push_back({.filter = int_filter::abs}); break;
          case bc_opcode::filter_int_hex:    ref.int_filters.push_back({.filter = int_filter::hex}); break;
          case bc_opcode::filter_int_oct:    ref.int_filters.push_back({.filter = int_filter::oct}); break;
          case bc_opcode::filter_int_bin:    ref.int_filters.push_back({.filter = int_filter::bin}); break;
          case bc_opcode::filter_int_neg:    ref.int_filters.push_back({.filter = int_filter::neg}); break;
          case bc_opcode::filter_int_mod:    ref.int_filters.push_back({.filter = int_filter::mod, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_numify: ref.int_filters.push_back({.filter = int_filter::numify}); break;
          case bc_opcode::filter_int_is_neg: ref.int_filters.push_back({.filter = int_filter::is_neg}); break;
          case bc_opcode::filter_int_eq:     ref.int_filters.push_back({.filter = int_filter::eq, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_ne:     ref.int_filters.push_back({.filter = int_filter::ne, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_gt:     ref.int_filters.push_back({.filter = int_filter::gt, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_gte:    ref.int_filters.push_back({.filter = int_filter::gte, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_lt:     ref.int_filters.push_back({.filter = int_filter::lt, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_lte:    ref.int_filters.push_back({.filter = int_filter::lte, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_zerofill: ref.int_filters.push_back({.filter = int_filter::zerofill, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_add: ref.int_filters.push_back({.filter = int_filter::add, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_sub: ref.int_filters.push_back({.filter = int_filter::sub, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_mul: ref.int_filters.push_back({.filter = int_filter::mul, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_int_div: ref.int_filters.push_back({.filter = int_filter::div, .arg = static_cast<int>(fi.operand)}); break;
          case bc_opcode::filter_float_precision: ref.float_filters.push_back({.filter = float_filter::precision, .arg = static_cast<int>(fi.operand)}); break;
          default: break;
          }
          ++j;
        } else {
          break;
        }
      }
    }
  }
  bc.error = ct.error;
  return bc;
}

template <class T, std::size_t N>
constexpr ct_parsed_template<N> resolve_field_indices(ct_parsed_template<N> tmpl) {
  tmpl.field_indices.fill(-1);
  if constexpr (ct_glz_reflectable<T>) {
    constexpr auto count = glz::reflect<T>::size;

    /** 比較演算子トークン（長さの多い順に並べて誤マッチを避ける） */
    struct compare_entry { std::string_view token; bc_opcode op; };
    constexpr auto compare_tokens = std::array{
      compare_entry{"==", bc_opcode::emit_if_eq},
      compare_entry{"!=", bc_opcode::emit_if_ne},
      compare_entry{">=", bc_opcode::emit_if_gte},
      compare_entry{"<=", bc_opcode::emit_if_lte},
      compare_entry{">",  bc_opcode::emit_if_gt},
      compare_entry{"<",  bc_opcode::emit_if_lt},
    };

    for (std::size_t i = 0; i < tmpl.size; ++i) {
      auto& idx = tmpl.field_indices[i];
      auto kind = tmpl.kinds[i];
      if (kind != ct_chunk_kind::placeholder && kind != ct_chunk_kind::section &&
          kind != ct_chunk_kind::inverted && kind != ct_chunk_kind::if_else) {
        continue;
      }

      /** if_else チャンクの比較演算子解析（enum 文字列→int 解決） */
      if (kind == ct_chunk_kind::if_else) {
        auto expr = tmpl.texts[i];
        for (auto const& cmp : compare_tokens) {
          auto op_pos = constexpr_find(expr, cmp.token);
          if (op_pos == std::string_view::npos) continue;
          auto lhs = trim_sv(expr.substr(0, op_pos));
          auto rhs = trim_sv(expr.substr(op_pos + cmp.token.size()));
          if (lhs.empty() || rhs.empty()) break;

          /** RHS が文字列リテラルの場合のみ enum 解決を試みる */
          auto str_lit = parse_string_literal(rhs);
          if (!str_lit) break;

          /** LHS フィールドインデックスを検索 */
          int lhs_idx = -1;
          [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
               if (std::string_view{glz::reflect<T>::keys[I]} == lhs) {
                 lhs_idx = static_cast<int>(I);
               }
             }()),
             ...);
          }(std::make_index_sequence<count>{});

          if (lhs_idx < 0) break;

          /** LHS フィールドが enum 型かどうか判定し、enchantum で RHS を解決 */
#ifndef INJAMM_NO_ENUM_REGISTRY
          [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
               if (static_cast<std::size_t>(lhs_idx) != I) return;
               using FT = std::remove_cvref_t<decltype(glz::get<I>(glz::to_tie(std::declval<T const&>())))>;
               if constexpr (std::is_enum_v<FT>) {
                 if (auto ev = enum_name_to_int<FT>(std::string_view{str_lit->data(), str_lit->size()})) {
                   tmpl.texts[i]    = lhs;
                   tmpl.flags[i]    = static_cast<std::uint8_t>(cmp.op);
                   tmpl.int_filters[i][0] = int_filter_entry{int_filter::eq, static_cast<int>(*ev)};
                   tmpl.int_filter_count[i] = 1;
                   idx = lhs_idx;
                 }
               }
             }()),
             ...);
          }(std::make_index_sequence<count>{});
#endif
          break;
        }
      }

      /** 通常のフィールドインデックス解決 */
      auto key = tmpl.texts[i];
      if (key.empty() || key.starts_with("loop.") || key == "root" || constexpr_find(key, '.') != std::string_view::npos) {
        continue;
      }
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
          if (std::string_view{glz::reflect<T>::keys[I]} == key) { idx = static_cast<int>(I); }
        }()), ...);
      }(std::make_index_sequence<count>{});
    }
  }
  return tmpl;
}

template <std::size_t N>
consteval void compile_chunk_range(ct_bytecode_builder<N>& b,
                                   ct_parsed_template<N> const& chunks,
                                   std::size_t start, std::size_t end);

template <class T, std::size_t N>
consteval ct_bytecode<N> ct_chunks_to_bytecode(ct_parsed_template<N> const& chunks) {
  ct_bytecode<N> bc;
  ct_bytecode_builder<N> b{bc};
  compile_chunk_range(b, chunks, 0, chunks.size);
  b.emit(bc_opcode::halt);
  return bc;
}

template <std::size_t N>
consteval void compile_chunk_range(ct_bytecode_builder<N>& b,
                                   ct_parsed_template<N> const& chunks,
                                   std::size_t start, std::size_t end) {
   auto emit_filter_chain = [&](std::size_t idx) {
     for (std::uint8_t f = 0; f < chunks.filter_count[idx]; ++f) {
       auto const& sf = chunks.filters[idx][f];
       switch (sf.filter) {
       case string_filter::upper:      b.emit(bc_opcode::filter_upper); break;
       case string_filter::lower:      b.emit(bc_opcode::filter_lower); break;
       case string_filter::capitalize: b.emit(bc_opcode::filter_capitalize); break;
       case string_filter::title:      b.emit(bc_opcode::filter_title); break;
       case string_filter::trim:       b.emit(bc_opcode::filter_trim); break;
       case string_filter::ltrim:      b.emit(bc_opcode::filter_ltrim); break;
       case string_filter::rtrim:      b.emit(bc_opcode::filter_rtrim); break;
       case string_filter::left:       b.emit(bc_opcode::filter_left, sf.arg1); break;
       case string_filter::right:      b.emit(bc_opcode::filter_right, sf.arg1); break;
       case string_filter::center:     b.emit(bc_opcode::filter_center, sf.arg1); break;
       case string_filter::truncate:   b.emit(bc_opcode::filter_truncate, sf.arg1); break;
       case string_filter::substr:     b.emit(bc_opcode::filter_substr, sf.arg1, sf.arg2); break;
        case string_filter::replace: {
          if (sf.str_arg1.empty()) {
            b.emit(bc_opcode::filter_replace);
          } else {
            auto old_idx = b.add_literal({sf.str_arg1.data(), sf.str_arg1.size()});
            auto new_idx = b.add_literal({sf.str_arg2.data(), sf.str_arg2.size()});
            b.emit(bc_opcode::filter_replace, old_idx, new_idx);
          }
          break;
        }
        case string_filter::default_value: {
          auto def_idx = b.add_literal({sf.str_arg1.data(), sf.str_arg1.size()});
          b.emit(bc_opcode::filter_default, def_idx);
          break;
        }
        case string_filter::to_json:
          b.emit(bc_opcode::filter_json);
          break;
        case string_filter::safe:
          b.emit(bc_opcode::filter_safe);
          break;
        case string_filter::indent:
          b.emit(bc_opcode::filter_indent, static_cast<std::uint32_t>(sf.arg1));
          break;
        case string_filter::pad: {
          std::uint32_t pad_idx = UINT32_MAX;
          if (!sf.str_arg1.empty()) {
            pad_idx = b.add_literal({sf.str_arg1.data(), sf.str_arg1.size()});
          }
          b.emit(bc_opcode::filter_pad, static_cast<std::uint32_t>(sf.arg1), pad_idx);
          break;
        }
        case string_filter::pluralize: {
          auto s_idx = b.add_literal({sf.str_arg1.data(), sf.str_arg1.size()});
          auto p_idx = b.add_literal({sf.str_arg2.data(), sf.str_arg2.size()});
          b.emit(bc_opcode::filter_pluralize, s_idx, p_idx);
          break;
        }
       }
      }
      for (std::uint8_t f = 0; f < chunks.int_filter_count[idx]; ++f) {
       auto const& intf = chunks.int_filters[idx][f];
       switch (intf.filter) {
       case int_filter::abs:      b.emit(bc_opcode::filter_int_abs); break;
       case int_filter::hex:      b.emit(bc_opcode::filter_int_hex); break;
       case int_filter::oct:      b.emit(bc_opcode::filter_int_oct); break;
       case int_filter::bin:      b.emit(bc_opcode::filter_int_bin); break;
       case int_filter::neg:      b.emit(bc_opcode::filter_int_neg); break;
       case int_filter::mod:      b.emit(bc_opcode::filter_int_mod, intf.arg); break;
       case int_filter::numify:   b.emit(bc_opcode::filter_int_numify); break;
       case int_filter::is_neg:   b.emit(bc_opcode::filter_int_is_neg); break;
       case int_filter::eq:       b.emit(bc_opcode::filter_int_eq, intf.arg); break;
       case int_filter::ne:       b.emit(bc_opcode::filter_int_ne, intf.arg); break;
       case int_filter::gt:       b.emit(bc_opcode::filter_int_gt, intf.arg); break;
       case int_filter::gte:      b.emit(bc_opcode::filter_int_gte, intf.arg); break;
       case int_filter::lt:       b.emit(bc_opcode::filter_int_lt, intf.arg); break;
       case int_filter::lte:      b.emit(bc_opcode::filter_int_lte, intf.arg); break;
        case int_filter::zerofill: b.emit(bc_opcode::filter_int_zerofill, intf.arg); break;
        case int_filter::add:      b.emit(bc_opcode::filter_int_add, intf.arg); break;
        case int_filter::sub:      b.emit(bc_opcode::filter_int_sub, intf.arg); break;
        case int_filter::mul:      b.emit(bc_opcode::filter_int_mul, intf.arg); break;
        case int_filter::div:      b.emit(bc_opcode::filter_int_div, intf.arg); break;
       }
     }
     for (std::uint8_t f = 0; f < chunks.float_filter_count[idx]; ++f) {
       auto const& ff = chunks.float_filters[idx][f];
       if (ff.filter == float_filter::precision)
         b.emit(bc_opcode::filter_float_precision, ff.arg);
     }
   };

   for (std::size_t i = start; i < end; ++i) {
     switch (chunks.kinds[i]) {
     case ct_chunk_kind::literal: {
      auto lit_idx = b.add_literal({chunks.texts[i].data(), chunks.texts[i].size()});
      b.emit(bc_opcode::emit_literal, lit_idx);
      break;
    }
      case ct_chunk_kind::placeholder: {
        auto sv = chunks.texts[i];
        bool raw = (chunks.flags[i] != 0);
        bool has_filters = (chunks.filter_count[i] > 0 || chunks.int_filter_count[i] > 0
                            || chunks.float_filter_count[i] > 0);

        // safe filter detection: force raw output
        bool has_safe = false;
        for (std::uint8_t f = 0; f < chunks.filter_count[i]; ++f) {
          if (chunks.filters[i][f].filter == string_filter::safe) { has_safe = true; break; }
        }
        bool use_raw = raw || has_safe;

        // {{this}} → emit_this (context serialization)
        if (sv == "this") {
          b.emit(bc_opcode::emit_this);
          break;
        }

        // root.field → resolve_filtered or emit_at_root_field
        if (sv.starts_with("root.")) {
         auto rest = sv.substr(5);
         auto vridx = b.add_var_ref({rest.data(), rest.size()},
                                     static_cast<std::uint32_t>(chunks.field_indices[i]));
         if (has_filters) {
           auto filter_count = static_cast<std::uint32_t>(chunks.filter_count[i] + chunks.int_filter_count[i] + chunks.float_filter_count[i]);
           b.emit(bc_opcode::resolve_filtered, filter_count, vridx);
           emit_filter_chain(i);
           b.emit(use_raw ? bc_opcode::emit_filtered_raw : bc_opcode::emit_filtered);
          } else {
            b.emit(use_raw ? bc_opcode::emit_at_root_field_raw : bc_opcode::emit_at_root_field, vridx);
          }
         break;
        }

        // {{field.size}} → emit_var_size
        if (sv.ends_with(".size") && !has_filters) {
          auto base_key = sv.substr(0, sv.size() - 5);
          auto vridx = b.add_var_ref({base_key.data(), base_key.size()},
                                      static_cast<std::uint32_t>(chunks.field_indices[i]));
          b.emit(bc_opcode::emit_var_size, vridx);
          break;
        }

        auto vridx = b.add_var_ref({sv.data(), sv.size()},
                                    static_cast<std::uint32_t>(chunks.field_indices[i]));

        if (has_filters) {
          auto filter_count = static_cast<std::uint32_t>(chunks.filter_count[i] + chunks.int_filter_count[i] + chunks.float_filter_count[i]);
          b.emit(bc_opcode::resolve_filtered, filter_count, vridx);
          emit_filter_chain(i);
          b.emit(use_raw ? bc_opcode::emit_filtered_raw : bc_opcode::emit_filtered);
          break;
        }

        // Fusion: preceding emit_literal + no-filters var → emit_litvar
        if (b.bc.instr_count > 0) {
          auto& prev = b.bc.instructions[b.bc.instr_count - 1];
          if (prev.op == bc_opcode::emit_literal) {
            prev.op = use_raw ? bc_opcode::emit_litvar_raw : bc_opcode::emit_litvar;
            prev.operand2 = vridx;
            break;
          }
        }

        b.emit(use_raw ? bc_opcode::emit_var_raw : bc_opcode::emit_var, vridx);
        break;
      }
    case ct_chunk_kind::section: {
      auto vridx = b.add_var_ref({chunks.texts[i].data(), chunks.texts[i].size()},
                                  static_cast<std::uint32_t>(chunks.field_indices[i]));
      auto sec_instr = b.current_offset();
      b.emit(bc_opcode::emit_section, 0, vridx);
      compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);
      if (chunks.else_starts[i] != 0) {
        auto tramp_instr = b.current_offset();
        b.emit(bc_opcode::emit_else, 0);
        b.bc.instructions[sec_instr].operand3 = static_cast<std::uint32_t>(b.current_offset());
        compile_chunk_range(b, chunks, chunks.else_starts[i], chunks.else_ends[i]);
        b.emit(bc_opcode::emit_end, static_cast<std::uint32_t>(sec_instr));
        b.patch_jump(sec_instr, static_cast<std::uint32_t>(b.current_offset()));
        b.patch_jump(tramp_instr, static_cast<std::uint32_t>(b.current_offset()));
      } else {
        b.emit(bc_opcode::emit_end, static_cast<std::uint32_t>(sec_instr));
        b.patch_jump(sec_instr, static_cast<std::uint32_t>(b.current_offset()));
      }
      std::size_t skip_to = chunks.else_starts[i] != 0 ? chunks.else_ends[i] : chunks.body_ends[i];
      i = skip_to - 1;
      break;
    }
    case ct_chunk_kind::inverted: {
      auto vridx = b.add_var_ref({chunks.texts[i].data(), chunks.texts[i].size()},
                                  static_cast<std::uint32_t>(chunks.field_indices[i]));
      auto inv_instr = b.current_offset();
      b.emit(bc_opcode::emit_inverted, 0, vridx);
      compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);
      if (chunks.else_starts[i] != 0) {
        auto tramp_instr = b.current_offset();
        b.emit(bc_opcode::emit_else, 0);
        b.bc.instructions[inv_instr].operand3 = static_cast<std::uint32_t>(b.current_offset());
        compile_chunk_range(b, chunks, chunks.else_starts[i], chunks.else_ends[i]);
        b.emit(bc_opcode::emit_end, static_cast<std::uint32_t>(inv_instr));
        b.patch_jump(inv_instr, static_cast<std::uint32_t>(b.current_offset()));
        b.patch_jump(tramp_instr, static_cast<std::uint32_t>(b.current_offset()));
      } else {
        b.emit(bc_opcode::emit_end, static_cast<std::uint32_t>(inv_instr));
        b.patch_jump(inv_instr, static_cast<std::uint32_t>(b.current_offset()));
      }
      std::size_t skip_to = chunks.else_starts[i] != 0 ? chunks.else_ends[i] : chunks.body_ends[i];
      i = skip_to - 1;
      break;
    }
    case ct_chunk_kind::at_var: {
      auto ak = static_cast<at_var_kind>(chunks.flags[i]);
      switch (ak) {
      case at_var_kind::index: b.emit(bc_opcode::emit_at_index); break;
      case at_var_kind::index1: b.emit(bc_opcode::emit_at_index1); break;
      case at_var_kind::size: b.emit(bc_opcode::emit_at_size); break;
      case at_var_kind::first: b.emit(bc_opcode::emit_at_first); break;
      case at_var_kind::last:  b.emit(bc_opcode::emit_at_last); break;
      case at_var_kind::key:   b.emit(bc_opcode::emit_at_key); break;
      }
      break;
    }
    case ct_chunk_kind::at_section: {
      bool inverted = (chunks.else_starts[i] != 0);
      auto body_op = inverted ? bc_opcode::emit_at_inverted : bc_opcode::emit_at_section;
      auto sec_instr = b.current_offset();
      // runtime kind encoding: 0=index, 1=first, 2=last
      std::uint32_t kind;
      switch (static_cast<at_var_kind>(chunks.flags[i])) {
        case at_var_kind::index: kind = 0; break;
        case at_var_kind::first: kind = 1; break;
        case at_var_kind::last:  kind = 2; break;
        default: kind = 0; break;
      }
      b.emit(body_op, 0, kind);
      compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);
      b.emit(bc_opcode::emit_end, static_cast<std::uint32_t>(sec_instr));
      b.patch_jump(sec_instr, static_cast<std::uint32_t>(b.current_offset()));
      i = chunks.body_ends[i] - 1;
      break;
    }
    case ct_chunk_kind::if_else: {
      auto sv = chunks.texts[i];

      /** 定数条件の最適化: リテラル整数はコンパイル時に真偽判定し、到達不可能な分岐のバイトコード生成を省略 */
      if (chunks.flags[i] == 0 && chunks.filter_count[i] == 0 &&
          chunks.int_filter_count[i] == 0 && chunks.float_filter_count[i] == 0) {
        auto check_expr = sv;
        bool negate = false;
        if (check_expr.starts_with("!")) {
          negate = true;
          check_expr = trim_sv(check_expr.substr(1));
        }
        if (auto int_val = parse_int_literal(check_expr)) {
          bool cond = negate ? (*int_val == 0) : (*int_val != 0);
          if (cond) {
            compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);
          } else {
            compile_chunk_range(b, chunks, chunks.else_starts[i], chunks.else_ends[i]);
          }
          std::size_t skip_to = chunks.else_ends[i] != 0 ? chunks.else_ends[i] : chunks.body_ends[i];
          i = skip_to - 1;
          break;
        }
      }

      bool has_else = (chunks.else_starts[i] != 0 || chunks.else_ends[i] != 0);
      auto field_idx = static_cast<std::uint32_t>(chunks.field_indices[i]);

      auto if_instr = b.current_offset();

      if (chunks.flags[i] != 0 && chunks.int_filter_count[i] > 0) {
        /** 比較演算子が解決済み（enum 文字列→int 変換済み）: emit_if_cmp を発行 */
        auto cmp_op = static_cast<bc_opcode>(chunks.flags[i]);
        auto vridx  = b.add_var_ref_cmp({sv.data(), sv.size()}, field_idx,
                                        chunks.int_filters[i][0].arg);
        b.emit(cmp_op, 0, vridx);
      } else {
        /** 通常の真偽判定 */
        auto vridx = b.add_var_ref({sv.data(), sv.size()}, field_idx);
        b.emit(bc_opcode::emit_if, 0, vridx);
      }

      compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);

      if (has_else) {
        auto else_instr = b.current_offset();
        b.emit(bc_opcode::emit_else, 0);
        compile_chunk_range(b, chunks, chunks.else_starts[i], chunks.else_ends[i]);
        auto endif_addr = b.current_offset();
        b.emit(bc_opcode::emit_endif);
        b.patch_jump(else_instr, static_cast<std::uint32_t>(endif_addr + 1));
        b.patch_jump(if_instr, static_cast<std::uint32_t>(else_instr + 1));
      } else {
        auto endif_addr = b.current_offset();
        b.emit(bc_opcode::emit_endif);
        b.patch_jump(if_instr, static_cast<std::uint32_t>(endif_addr + 1));
      }
      std::size_t skip_to = chunks.else_ends[i] != 0 ? chunks.else_ends[i] : chunks.body_ends[i];
      i = skip_to - 1;
      break;
    }
     case ct_chunk_kind::ct_break:
       b.emit(bc_opcode::emit_break);
       break;
     case ct_chunk_kind::ct_continue:
       b.emit(bc_opcode::emit_continue);
       break;
     default: break;
    }
  }
}

} // namespace injamm::detail
