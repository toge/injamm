#pragma once

#include "ct_chunk.hpp"
#include "glz_dispatch.hpp"
#include "types.hpp"
#include "bytecode.hpp"
#include "bytecode_exec.hpp"

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
    bc.var_refs.push_back(std::move(ref));
  }
  bc.error = ct.error;
  return bc;
}

template <class T, std::size_t N>
constexpr ct_parsed_template<N> resolve_field_indices(ct_parsed_template<N> tmpl) {
  tmpl.field_indices.fill(-1);
  if constexpr (glz_reflectable<T>) {
    constexpr auto count = glz::reflect<T>::size;
    for (std::size_t i = 0; i < tmpl.size; ++i) {
      auto& idx = tmpl.field_indices[i];
      auto kind = tmpl.kinds[i];
      if (kind != ct_chunk_kind::placeholder && kind != ct_chunk_kind::section &&
          kind != ct_chunk_kind::inverted && kind != ct_chunk_kind::if_else) {
        continue;
      }
      auto key = tmpl.texts[i];
      if (key.empty() || key[0] == '@' || key.find('.') != std::string_view::npos) {
        continue;
      }
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((std::string_view{glz::reflect<T>::keys[I]} == key && (idx = static_cast<int>(I), true)) || ...);
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
  return bc;
}

template <std::size_t N>
consteval void compile_chunk_range(ct_bytecode_builder<N>& b,
                                   ct_parsed_template<N> const& chunks,
                                   std::size_t start, std::size_t end) {
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

      // @root.field → emit_at_root_field (strip @root. prefix)
      if (sv.starts_with("@root.")) {
        auto rest = sv.substr(6);
        auto vridx = b.add_var_ref({rest.data(), rest.size()},
                                    static_cast<std::uint32_t>(chunks.field_indices[i]));
        b.emit(raw ? bc_opcode::emit_at_root_field_raw : bc_opcode::emit_at_root_field, 0, vridx);
        break;
      }

      auto vridx = b.add_var_ref({sv.data(), sv.size()},
                                  static_cast<std::uint32_t>(chunks.field_indices[i]));

      // Fusion: preceding emit_literal + no-filters var → emit_litvar
      if (b.bc.instr_count > 0) {
        auto& prev = b.bc.instructions[b.bc.instr_count - 1];
        if (prev.op == bc_opcode::emit_literal) {
          prev.op = raw ? bc_opcode::emit_litvar_raw : bc_opcode::emit_litvar;
          prev.operand2 = vridx;
          break;
        }
      }

      b.emit(raw ? bc_opcode::emit_var_raw : bc_opcode::emit_var, 0, vridx);
      break;
    }
    case ct_chunk_kind::section: {
      auto vridx = b.add_var_ref({chunks.texts[i].data(), chunks.texts[i].size()},
                                  static_cast<std::uint32_t>(chunks.field_indices[i]));
      auto sec_instr = b.current_offset();
      b.emit(bc_opcode::emit_section, 0, vridx);
      compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);
      b.emit(bc_opcode::emit_end, static_cast<std::uint32_t>(sec_instr));
      b.patch_jump(sec_instr, static_cast<std::uint32_t>(b.current_offset()));
      i = chunks.body_ends[i] - 1;
      break;
    }
    case ct_chunk_kind::inverted: {
      auto vridx = b.add_var_ref({chunks.texts[i].data(), chunks.texts[i].size()},
                                  static_cast<std::uint32_t>(chunks.field_indices[i]));
      auto inv_instr = b.current_offset();
      b.emit(bc_opcode::emit_inverted, 0, vridx);
      compile_chunk_range(b, chunks, chunks.body_starts[i], chunks.body_ends[i]);
      b.emit(bc_opcode::emit_end, static_cast<std::uint32_t>(inv_instr));
      b.patch_jump(inv_instr, static_cast<std::uint32_t>(b.current_offset()));
      i = chunks.body_ends[i] - 1;
      break;
    }
    default: break;
    }
  }
  b.emit(bc_opcode::halt);
}

} // namespace injamm::detail
