#pragma once

#include "ct_chunk.hpp"
#include "ct_render.hpp"
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

template <std::size_t N>
struct ct_bytecode {
  std::array<bc_instruction, N> instructions{};
  std::array<string_ref, N> literals{};
  std::array<ct_var_ref, N> var_refs{};
  std::size_t instr_count{};
  std::size_t literal_count{};
  std::size_t var_ref_count{};
  error_ctx error{};
};

template <std::size_t N>
struct ct_bytecode_builder {
  ct_bytecode<N>& bc;

  std::uint32_t add_literal(string_ref lit) {
    auto idx = static_cast<std::uint32_t>(bc.literal_count);
    bc.literals[bc.literal_count] = lit;
    ++bc.literal_count;
    return idx;
  }

  std::uint32_t add_var_ref(string_ref key, std::uint32_t field_index) {
    auto idx = static_cast<std::uint32_t>(bc.var_ref_count);
    bc.var_refs[bc.var_ref_count] = {key, field_index};
    ++bc.var_ref_count;
    return idx;
  }

  void emit(bc_opcode op, std::uint32_t operand = 0, std::uint32_t operand2 = 0) {
    bc.instructions[bc.instr_count] = {op, operand, operand2};
    ++bc.instr_count;
  }

  std::size_t current_offset() const { return bc.instr_count; }

  void patch_jump(std::size_t idx, std::uint32_t target) {
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
    bc.literals.emplace_back(ct.literals[i].data, ct.literals[i].size);
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
    default: break;
    }
  }
}

} // namespace injamm::detail
