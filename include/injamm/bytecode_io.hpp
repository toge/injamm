#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "bytecode.hpp"
#include "glz_dispatch.hpp"

namespace injamm {

[[nodiscard]] error_code save_bytecode(detail::bytecode const& bc, std::ostream& os);

template <class T>
[[nodiscard]] expected<detail::bytecode> load_bytecode(std::istream& is);

namespace detail {

// ---- write helpers ----

inline void write_u8(std::ostream& os, std::uint8_t v) { os.put(static_cast<char>(v)); }

inline void write_u32_le(std::ostream& os, std::uint32_t v) {
  os.put(static_cast<char>(v & 0xFF));
  os.put(static_cast<char>((v >> 8) & 0xFF));
  os.put(static_cast<char>((v >> 16) & 0xFF));
  os.put(static_cast<char>((v >> 24) & 0xFF));
}

inline void write_u64_le(std::ostream& os, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    os.put(static_cast<char>(v & 0xFF));
    v >>= 8;
  }
}

inline void write_i32_le(std::ostream& os, std::int32_t v) {
  write_u32_le(os, static_cast<std::uint32_t>(v));
}

inline void write_string(std::ostream& os, std::string_view s) {
  write_u64_le(os, s.size());
  os.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// ---- struct serializers ----

inline void write_int_filter_entry(std::ostream& os, int_filter_entry const& e) {
  write_u8(os, static_cast<std::uint8_t>(e.filter));
  write_i32_le(os, e.arg);
}

inline void write_float_filter_entry(std::ostream& os, float_filter_entry const& e) {
  write_u8(os, static_cast<std::uint8_t>(e.filter));
  write_i32_le(os, e.arg);
}

inline void write_string_filter_entry(std::ostream& os, std::vector<std::string> const& literals,
                                       string_filter_entry const& e) {
  write_u8(os, static_cast<std::uint8_t>(e.filter));
  write_i32_le(os, e.arg1);
  write_i32_le(os, e.arg2);
  // str_arg1: save as literal index (UINT64_MAX = empty)
  auto it1 = std::find_if(literals.begin(), literals.end(),
                          [&](auto const& s) { return s == e.str_arg1; });
  write_u64_le(os, it1 != literals.end()
                       ? static_cast<std::uint64_t>(std::distance(literals.begin(), it1))
                       : UINT64_MAX);
  // str_arg2
  auto it2 = std::find_if(literals.begin(), literals.end(),
                          [&](auto const& s) { return s == e.str_arg2; });
  write_u64_le(os, it2 != literals.end()
                       ? static_cast<std::uint64_t>(std::distance(literals.begin(), it2))
                       : UINT64_MAX);
}

inline void write_var_ref(std::ostream& os, std::vector<std::string> const& literals,
                           bc_var_ref const& ref) {
  write_string(os, ref.key);
  write_u8(os, ref.has_dot ? 1 : 0);
  write_u8(os, ref.is_loop_parent ? 1 : 0);
  write_u8(os, static_cast<std::uint8_t>(ref.compare_rhs_kind));
  write_string(os, ref.compare_rhs_text);
  write_u8(os, ref.compare_rhs_has_dot ? 1 : 0);
  write_u8(os, ref.filter_flags);

  write_u64_le(os, ref.filters.size());
  for (auto const& f : ref.filters) write_string_filter_entry(os, literals, f);

  write_u64_le(os, ref.int_filters.size());
  for (auto const& f : ref.int_filters) write_int_filter_entry(os, f);

  write_u64_le(os, ref.float_filters.size());
  for (auto const& f : ref.float_filters) write_float_filter_entry(os, f);
}

inline void write_instruction(std::ostream& os, bc_instruction const& inst) {
  write_u8(os, static_cast<std::uint8_t>(inst.op));
  write_u32_le(os, inst.operand);
  write_u32_le(os, inst.operand2);
  write_u32_le(os, inst.operand3);
}

// forward declarations for recursive partial
void write_bytecode(std::ostream& os, bytecode const& bc);
void write_partial_entry(std::ostream& os, partial_entry const& pe);

inline void write_partial_entry(std::ostream& os, partial_entry const& pe) {
  write_string(os, pe.name);
  write_u8(os, pe.local ? 1 : 0);
  if (pe.bc) {
    write_bytecode(os, *pe.bc);
  } else {
    bytecode empty;
    write_bytecode(os, empty);
  }
}

inline void write_bytecode(std::ostream& os, bytecode const& bc) {
  write_u8(os, bc.is_simple ? 1 : 0);
  write_u64_le(os, bc.literal_total_size);

  write_u64_le(os, bc.instructions.size());
  for (auto const& inst : bc.instructions) write_instruction(os, inst);

  write_u64_le(os, bc.literals.size());
  for (auto const& lit : bc.literals) write_string(os, lit);

  write_u64_le(os, bc.var_refs.size());
  for (auto const& ref : bc.var_refs) write_var_ref(os, bc.literals, ref);

  write_u64_le(os, bc.partial_entries.size());
  for (auto const& pe : bc.partial_entries) write_partial_entry(os, pe);
}

// ---- read helpers ----

struct read_state {
  bool ok = true;
  error_code ec = error_code::none;
};

inline std::uint8_t read_u8(std::istream& is, read_state& state) {
  if (!state.ok) return 0;
  auto c = is.get();
  if (c == std::char_traits<char>::eof()) { state.ok = false; state.ec = error_code::no_read_input; return 0; }
  return static_cast<std::uint8_t>(c);
}

inline std::uint32_t read_u32_le(std::istream& is, read_state& state) {
  if (!state.ok) return 0;
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i) {
    auto c = is.get();
    if (c == std::char_traits<char>::eof()) { state.ok = false; state.ec = error_code::no_read_input; return 0; }
    v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << (i * 8);
  }
  return v;
}

inline std::uint64_t read_u64_le(std::istream& is, read_state& state) {
  if (!state.ok) return 0;
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    auto c = is.get();
    if (c == std::char_traits<char>::eof()) { state.ok = false; state.ec = error_code::no_read_input; return 0; }
    v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(c)) << (i * 8);
  }
  return v;
}

inline std::int32_t read_i32_le(std::istream& is, read_state& state) {
  return static_cast<std::int32_t>(read_u32_le(is, state));
}

inline std::string read_string(std::istream& is, read_state& state) {
  auto len = read_u64_le(is, state);
  if (!state.ok) return {};
  std::string s(static_cast<std::size_t>(len), '\0');
  is.read(s.data(), static_cast<std::streamsize>(len));
  if (is.gcount() != static_cast<std::streamsize>(len)) { state.ok = false; state.ec = error_code::no_read_input; }
  return s;
}

inline bc_instruction read_instruction(std::istream& is, read_state& state) {
  bc_instruction inst;
  inst.op = static_cast<bc_opcode>(read_u8(is, state));
  inst.operand = read_u32_le(is, state);
  inst.operand2 = read_u32_le(is, state);
  inst.operand3 = read_u32_le(is, state);
  return inst;
}

inline string_filter_entry read_string_filter_entry(std::istream& is, read_state& state,
                                                      std::vector<std::string> const& literals) {
  string_filter_entry e;
  e.filter = static_cast<string_filter>(read_u8(is, state));
  e.arg1 = read_i32_le(is, state);
  e.arg2 = read_i32_le(is, state);
  auto idx1 = read_u64_le(is, state);
  auto idx2 = read_u64_le(is, state);
  if (state.ok) {
    if (idx1 < literals.size()) e.str_arg1 = literals[static_cast<std::size_t>(idx1)];
    if (idx2 < literals.size()) e.str_arg2 = literals[static_cast<std::size_t>(idx2)];
  }
  return e;
}

inline int_filter_entry read_int_filter_entry(std::istream& is, read_state& state) {
  int_filter_entry e;
  e.filter = static_cast<int_filter>(read_u8(is, state));
  e.arg = read_i32_le(is, state);
  return e;
}

inline float_filter_entry read_float_filter_entry(std::istream& is, read_state& state) {
  float_filter_entry e;
  e.filter = static_cast<float_filter>(read_u8(is, state));
  e.arg = read_i32_le(is, state);
  return e;
}

// ---- field index re-resolution (T-dependent) ----

template <class T>
std::uint32_t resolve_field_idx(std::string_view key) {
  if constexpr (ct_glz_reflectable<T>) {
    constexpr auto sz = static_cast<std::size_t>(glz::reflect<T>::size);
    for (std::size_t i = 0; i < sz; ++i) {
      if (std::string_view{glz::reflect<T>::keys[i]} == key) {
        return static_cast<std::uint32_t>(i);
      }
    }
  }
  return UINT32_MAX;
}

template <class T>
void re_resolve_var_ref(bc_var_ref& ref) {
  if (ref.key.empty()) return;

  if (ref.has_dot) {
    auto dot = ref.key.find('.');
    auto first = std::string_view{ref.key}.substr(0, dot);
    ref.field_index = resolve_field_idx<T>(first);
  } else if (!ref.is_loop_parent) {
    ref.field_index = resolve_field_idx<T>(ref.key);
  }

  if (ref.compare_rhs_kind == compare_operand_kind::variable && !ref.compare_rhs_text.empty()) {
    if (ref.compare_rhs_has_dot) {
      auto dot = ref.compare_rhs_text.find('.');
      auto first = std::string_view{ref.compare_rhs_text}.substr(0, dot);
      ref.compare_rhs_field_index = resolve_field_idx<T>(first);
    } else {
      ref.compare_rhs_field_index = resolve_field_idx<T>(ref.compare_rhs_text);
    }
  }
}

// forward declaration
template <class T>
bytecode read_bytecode_body(std::istream& is, read_state& state);

} // namespace detail

inline error_code save_bytecode(detail::bytecode const& bc, std::ostream& os) {
  constexpr char magic[] = {'I', 'J', 'B', 'C'};
  os.write(magic, 4);
  detail::write_u32_le(os, 1); // version
  if (!os) return error_code::no_read_input;

  detail::write_bytecode(os, bc);
  if (!os) return error_code::no_read_input;

  return error_code::none;
}

template <class T>
expected<detail::bytecode> load_bytecode(std::istream& is) {
  detail::read_state state;

  char magic[4]{};
  is.read(magic, 4);
  if (is.gcount() != 4)
    return std::unexpected(error_ctx{0, error_code::no_read_input, "Failed to read magic"});
  if (magic[0] != 'I' || magic[1] != 'J' || magic[2] != 'B' || magic[3] != 'C')
    return std::unexpected(error_ctx{0, error_code::syntax_error, "Invalid magic"});

  auto version = detail::read_u32_le(is, state);
  if (!state.ok)
    return std::unexpected(error_ctx{0, state.ec, "Failed to read version"});
  if (version != 1)
    return std::unexpected(error_ctx{0, error_code::type_mismatch, "Unsupported bytecode version"});

  auto bc = detail::read_bytecode_body<T>(is, state);
  if (!state.ok)
    return std::unexpected(error_ctx{0, state.ec, "Failed to read bytecode"});

  return bc;
}

namespace detail {

template <class T>
bytecode read_bytecode_body(std::istream& is, read_state& state) {
  bytecode bc;

  bc.is_simple = read_u8(is, state) != 0;
  bc.literal_total_size = read_u64_le(is, state);
  if (!state.ok) return bc;

  // Instructions
  auto ic = read_u64_le(is, state);
  if (!state.ok) return bc;
  bc.instructions.reserve(static_cast<std::size_t>(ic));
  for (std::uint64_t i = 0; i < ic; ++i)
    bc.instructions.push_back(read_instruction(is, state));

  // Literals
  auto lc = read_u64_le(is, state);
  if (!state.ok) return bc;
  bc.literals.reserve(static_cast<std::size_t>(lc));
  for (std::uint64_t i = 0; i < lc; ++i)
    bc.literals.push_back(read_string(is, state));

  // Var refs
  auto vc = read_u64_le(is, state);
  if (!state.ok) return bc;
  bc.var_refs.reserve(static_cast<std::size_t>(vc));
  for (std::uint64_t i = 0; i < vc; ++i) {
    auto key = read_string(is, state);
    auto has_dot = read_u8(is, state) != 0;
    auto is_loop_parent = read_u8(is, state) != 0;
    auto cmp_kind = static_cast<compare_operand_kind>(read_u8(is, state));
    auto cmp_text = read_string(is, state);
    auto cmp_has_dot = read_u8(is, state) != 0;
    auto filter_flags = read_u8(is, state);
    if (!state.ok) return bc;

    bc_var_ref ref;
    ref.key = std::move(key);
    ref.has_dot = has_dot;
    ref.is_loop_parent = is_loop_parent;
    ref.compare_rhs_kind = cmp_kind;
    ref.compare_rhs_text = std::move(cmp_text);
    ref.compare_rhs_has_dot = cmp_has_dot;
    ref.filter_flags = filter_flags;

    // String filters
    auto fc = read_u64_le(is, state);
    if (!state.ok) return bc;
    ref.filters.reserve(static_cast<std::size_t>(fc));
    for (std::uint64_t j = 0; j < fc; ++j)
      ref.filters.push_back(read_string_filter_entry(is, state, bc.literals));

    // Int filters
    auto ifc = read_u64_le(is, state);
    if (!state.ok) return bc;
    ref.int_filters.reserve(static_cast<std::size_t>(ifc));
    for (std::uint64_t j = 0; j < ifc; ++j)
      ref.int_filters.push_back(read_int_filter_entry(is, state));

    // Float filters
    auto ffc = read_u64_le(is, state);
    if (!state.ok) return bc;
    ref.float_filters.reserve(static_cast<std::size_t>(ffc));
    for (std::uint64_t j = 0; j < ffc; ++j)
      ref.float_filters.push_back(read_float_filter_entry(is, state));

    re_resolve_var_ref<T>(ref);

    bc.var_refs.push_back(std::move(ref));
  }

  // Partial entries (recursive)
  auto pc = read_u64_le(is, state);
  if (!state.ok) return bc;
  bc.partial_entries.reserve(static_cast<std::size_t>(pc));
  for (std::uint64_t i = 0; i < pc; ++i) {
    auto name = read_string(is, state);
    auto local = read_u8(is, state) != 0;
    auto partial_bc = std::make_shared<bytecode>(read_bytecode_body<T>(is, state));
    bc.partial_entries.push_back(partial_entry{std::move(name), std::move(partial_bc), local});
  }

  return bc;
}

} // namespace detail
} // namespace injamm
