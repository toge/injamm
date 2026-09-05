/**
 * @file bytecode_io.hpp
 * @brief バイトコードのバイナリ保存・読み込み
 *
 * @details コンパイル済みの bytecode をバイナリ形式でストリームに保存し、
 *          後で読み込んで再利用するための関数を提供する。
 *          フォーマット: マジック "IJBC" + バージョン 1（リトルエンディアン）。
 *          field_index は保存せず、読み込み時に glaze リフレクションで再解決する。
 *
 *          INJAMM_NO_BYTECODE_IO が定義されている場合は istream/ostream API を
 *          除外し、span ベースの save_bytecode/load_bytecode のみを提供する。
 */

#pragma once

#include "config.hpp"

#include <cstddef>
#include <cstdint>
#ifndef INJAMM_NO_BYTECODE_IO
#include <istream>
#include <ostream>
#endif
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "bytecode.hpp"
#include "glz_dispatch.hpp"

namespace injamm {

#ifndef INJAMM_NO_BYTECODE_IO
/** @brief バイトコードをバイナリ形式でストリームに保存する */
[[nodiscard]] error_code save_bytecode(detail::bytecode const& bc, std::ostream& os);

/**
 * @brief バイナリ形式からバイトコードを読み込む
 *
 * @tparam T コンテキスト型（field_index 再解決に使用）
 * @param is 入力ストリーム
 * @return expected<detail::bytecode> 読み込まれたバイトコード、またはエラー
 */
template <class T>
[[nodiscard]] expected<detail::bytecode> load_bytecode(std::istream& is);
#endif // !INJAMM_NO_BYTECODE_IO

namespace detail {

#ifndef INJAMM_NO_BYTECODE_IO
// ---- ストリームベース書き込みヘルパ ----

/** @brief 1バイト書き込み */
inline void write_u8(std::ostream& os, std::uint8_t v) { os.put(static_cast<char>(v)); }

/** @brief 32ビット符号なし整数をリトルエンディアンで書き込み */
inline void write_u32_le(std::ostream& os, std::uint32_t v) {
  os.put(static_cast<char>(v & 0xFF));
  os.put(static_cast<char>((v >> 8) & 0xFF));
  os.put(static_cast<char>((v >> 16) & 0xFF));
  os.put(static_cast<char>((v >> 24) & 0xFF));
}

/** @brief 64ビット符号なし整数をリトルエンディアンで書き込み */
inline void write_u64_le(std::ostream& os, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    os.put(static_cast<char>(v & 0xFF));
    v >>= 8;
  }
}

/** @brief 32ビット符号あり整数をリトルエンディアンで書き込み */
inline void write_i32_le(std::ostream& os, std::int32_t v) {
  write_u32_le(os, static_cast<std::uint32_t>(v));
}

/** @brief 文字列を長さ前置で書き込み（サイズ(64bit) + 実データ） */
inline void write_string(std::ostream& os, std::string_view s) {
  write_u64_le(os, s.size());
  os.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// ---- 構造体シリアライザ ----

/** @brief int_filter_entry を書き込み（フィルタ種別 + 引数） */
inline void write_int_filter_entry(std::ostream& os, int_filter_entry const& e) {
  write_u8(os, static_cast<std::uint8_t>(e.filter));
  write_i32_le(os, e.arg);
}

/** @brief float_filter_entry を書き込み（フィルタ種別 + 引数） */
inline void write_float_filter_entry(std::ostream& os, float_filter_entry const& e) {
  write_u8(os, static_cast<std::uint8_t>(e.filter));
  write_i32_le(os, e.arg);
}

/** @brief string_filter_entry を書き込み（文字列引数はリテラルインデックスに変換） */
inline void write_string_filter_entry(std::ostream& os, std::vector<std::string> const& literals,
                                       string_filter_entry const& e) {
  write_u8(os, static_cast<std::uint8_t>(e.filter));
  write_i32_le(os, e.arg1);
  write_i32_le(os, e.arg2);
  // str_arg1: リテラルインデックスとして保存（見つからない場合は UINT64_MAX）
  auto it1 = std::find_if(literals.begin(), literals.end(),
                          [&](auto const& s) { return s == e.str_arg1; });
  write_u64_le(os, it1 != literals.end()
                       ? static_cast<std::uint64_t>(std::distance(literals.begin(), it1))
                       : UINT64_MAX);
  // str_arg2: 同上
  auto it2 = std::find_if(literals.begin(), literals.end(),
                          [&](auto const& s) { return s == e.str_arg2; });
  write_u64_le(os, it2 != literals.end()
                       ? static_cast<std::uint64_t>(std::distance(literals.begin(), it2))
                       : UINT64_MAX);
}

/** @brief bc_var_ref を書き込み（キー・フィルタ・比較情報など全フィールド） */
inline void write_var_ref(std::ostream& os, std::vector<std::string> const& literals,
                           bc_var_ref const& ref) {
  write_string(os, ref.key);
  write_u8(os, ref.has_dot ? 1 : 0);
  write_u8(os, ref.is_loop_parent ? 1 : 0);
  write_u8(os, static_cast<std::uint8_t>(ref.compare_rhs_kind));
  write_string(os, ref.compare_rhs_text);
  write_u8(os, ref.compare_rhs_has_dot ? 1 : 0);
  write_u8(os, ref.filter_flags);
  write_u8(os, ref.section_op_count);
  for (std::uint8_t i = 0; i < ref.section_op_count; ++i) {
    write_u8(os, static_cast<std::uint8_t>(ref.section_ops[i].kind));
    write_u32_le(os, static_cast<std::uint32_t>(ref.section_ops[i].arg));
    write_u32_le(os, static_cast<std::uint32_t>(ref.section_ops[i].arg2));
    // str_arg1: リテラルインデックスとして保存（v5 で追加、join の separator 用）
    auto it1 = std::find_if(literals.begin(), literals.end(),
                            [&](auto const& s) { return s == ref.section_ops[i].str_arg1; });
    write_u64_le(os, it1 != literals.end()
                         ? static_cast<std::uint64_t>(std::distance(literals.begin(), it1))
                         : UINT64_MAX);
  }

  write_u64_le(os, ref.filters.size());
  for (auto const& f : ref.filters) write_string_filter_entry(os, literals, f);

  write_u64_le(os, ref.int_filters.size());
  for (auto const& f : ref.int_filters) write_int_filter_entry(os, f);

  write_u64_le(os, ref.float_filters.size());
  for (auto const& f : ref.float_filters) write_float_filter_entry(os, f);

  /** v6: ルート型フォールバックフラグ（root_fb_*） */
  write_u8(os, ref.root_fallback);
}

/** @brief bc_instruction を書き込み（opcode + 3 オペランド） */
inline void write_instruction(std::ostream& os, bc_instruction const& inst) {
  write_u8(os, static_cast<std::uint8_t>(inst.op));
  write_u32_le(os, inst.operand);
  write_u32_le(os, inst.operand2);
  write_u32_le(os, inst.operand3);
}

// 前方宣言（再帰的な partial 対応）
void write_bytecode(std::ostream& os, bytecode const& bc);
void write_partial_entry(std::ostream& os, partial_entry const& pe);

/** @brief partial_entry を書き込み（名前・local フラグ + 内部バイトコードを再帰的に書き込み） */
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

/** @brief bytecode 全体を書き込み（シンプルフラグ・命令列・リテラル・変数参照・partial） */
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

// ---- 読み込みヘルパ ----

/** @brief 読み込み状態（エラーフラグ + エラーコード + バージョン） */
struct read_state {
  bool ok = true;
  error_code ec = error_code::none;
  int version = 1;
};

/** @brief vector::reserve を例外安全に試行 */
template <class Vec>
inline bool try_reserve(Vec& v, std::size_t n, read_state& st) noexcept {
#if INJAMM_HAS_EXCEPTIONS
  try {
    v.reserve(n);
    return true;
  } catch (...) {
    st.ok = false;
    st.ec = error_code::out_of_memory;
    return false;
  }
#else
  if (n > v.max_size()) { st.ok = false; st.ec = error_code::out_of_memory; return false; }
  v.reserve(n);
  return true;
#endif
}

/** @brief string::assign(len,'\0') を例外安全に試行 */
inline bool try_assign(std::string& s, std::size_t len, read_state& st) noexcept {
#if INJAMM_HAS_EXCEPTIONS
  try {
    s.assign(len, '\0');
    return true;
  } catch (...) {
    st.ok = false;
    st.ec = error_code::out_of_memory;
    return false;
  }
#else
  if (len > s.max_size()) { st.ok = false; st.ec = error_code::out_of_memory; return false; }
  s.assign(len, '\0');
  return true;
#endif
}

/** @brief 1バイト読み込み */
inline std::uint8_t read_u8(std::istream& is, read_state& state) {
  if (!state.ok) return 0;
  auto c = is.get();
  if (c == std::char_traits<char>::eof()) { state.ok = false; state.ec = error_code::no_read_input; return 0; }
  return static_cast<std::uint8_t>(c);
}

/** @brief 32ビット符号なし整数をリトルエンディアンで読み込み */
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

/** @brief 64ビット符号なし整数をリトルエンディアンで読み込み */
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

/** @brief 32ビット符号あり整数をリトルエンディアンで読み込み */
inline std::int32_t read_i32_le(std::istream& is, read_state& state) {
  return static_cast<std::int32_t>(read_u32_le(is, state));
}

/** @brief 文字列を長さ前置で読み込み */
inline std::string read_string(std::istream& is, read_state& state) {
  auto len = read_u64_le(is, state);
  if (!state.ok) return {};
  constexpr std::uint64_t max_string_len = 16 * 1024 * 1024; // 16 MiB
  if (len > max_string_len) { state.ok = false; state.ec = error_code::syntax_error; return {}; }
  std::string s;
  if (!try_assign(s, static_cast<std::size_t>(len), state)) return {};
  if (len > 0) {
    is.read(s.data(), static_cast<std::streamsize>(len));
    if (is.gcount() != static_cast<std::streamsize>(len)) { state.ok = false; state.ec = error_code::no_read_input; }
  }
  return s;
}
#endif // !INJAMM_NO_BYTECODE_IO

/** @brief オペコードが有効な範囲か確認 */
inline bool is_valid_opcode(bc_opcode op) noexcept {
  auto const value = static_cast<std::underlying_type_t<bc_opcode>>(op);
  auto const min = static_cast<std::underlying_type_t<bc_opcode>>(bc_opcode::emit_literal);
  auto const max = static_cast<std::underlying_type_t<bc_opcode>>(bc_opcode::halt);
  return value >= min && value <= max;
}

#ifndef INJAMM_NO_BYTECODE_IO
/** @brief bc_instruction を読み込み */
inline bc_instruction read_instruction(std::istream& is, read_state& state) {
  bc_instruction inst;
  inst.op = static_cast<bc_opcode>(read_u8(is, state));
  if (state.ok && !is_valid_opcode(inst.op)) {
    state.ok = false;
    state.ec = error_code::syntax_error;
  }
  inst.operand = read_u32_le(is, state);
  inst.operand2 = read_u32_le(is, state);
  inst.operand3 = read_u32_le(is, state);
  return inst;
}

/** @brief string_filter_entry を読み込み（文字列引数はリテラルインデックスから復元） */
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

/** @brief int_filter_entry を読み込み */
inline int_filter_entry read_int_filter_entry(std::istream& is, read_state& state) {
  int_filter_entry e;
  e.filter = static_cast<int_filter>(read_u8(is, state));
  e.arg = read_i32_le(is, state);
  return e;
}

/** @brief float_filter_entry を読み込み */
inline float_filter_entry read_float_filter_entry(std::istream& is, read_state& state) {
  float_filter_entry e;
  e.filter = static_cast<float_filter>(read_u8(is, state));
  e.arg = read_i32_le(is, state);
  return e;
}
#endif // !INJAMM_NO_BYTECODE_IO

// ---- field_index 再解決（T に依存） ----

/** @brief glaze リフレクションでキーに対応するフィールドインデックスを解決 */
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

/** @brief 読み込んだ bc_var_ref の field_index を T のリフレクションで再解決 */
template <class T>
void re_resolve_var_ref(bc_var_ref& ref) {
  if (ref.key.empty()) return;

  /** special 分類はシリアライズされないため読み込み時に再計算する */
  ref.special = classify_special_var(ref.key);

  if (ref.has_dot) {
    // ドット付きパス: 先頭セグメントのみ解決
    auto dot = ref.key.find('.');
    auto first = std::string_view{ref.key}.substr(0, dot);
    ref.field_index = resolve_field_idx<T>(first);
  } else if (!ref.is_loop_parent) {
    ref.field_index = resolve_field_idx<T>(ref.key);
  }

  // 比較演算子の右辺が変数の場合も同様に解決
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

#ifndef INJAMM_NO_BYTECODE_IO
// 前方宣言
template <class T>
bytecode read_bytecode_body(std::istream& is, read_state& state, int depth = 0);
#endif // !INJAMM_NO_BYTECODE_IO

// ---- span ベース書き込みヘルパ (vector<uint8_t>&、ストリーム不要) ----

inline void write_u8(std::vector<std::uint8_t>& buf, std::uint8_t v) { buf.push_back(v); }

inline void write_u32_le(std::vector<std::uint8_t>& buf, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) { buf.push_back(static_cast<std::uint8_t>(v & 0xFF)); v >>= 8; }
}

inline void write_u64_le(std::vector<std::uint8_t>& buf, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) { buf.push_back(static_cast<std::uint8_t>(v & 0xFF)); v >>= 8; }
}

inline void write_i32_le(std::vector<std::uint8_t>& buf, std::int32_t v) {
  write_u32_le(buf, static_cast<std::uint32_t>(v));
}

inline void write_string(std::vector<std::uint8_t>& buf, std::string_view s) {
  write_u64_le(buf, s.size());
  buf.insert(buf.end(), reinterpret_cast<std::uint8_t const*>(s.data()),
             reinterpret_cast<std::uint8_t const*>(s.data()) + s.size());
}

inline void write_int_filter_entry(std::vector<std::uint8_t>& buf, int_filter_entry const& e) {
  write_u8(buf, static_cast<std::uint8_t>(e.filter));
  write_i32_le(buf, e.arg);
}

inline void write_float_filter_entry(std::vector<std::uint8_t>& buf, float_filter_entry const& e) {
  write_u8(buf, static_cast<std::uint8_t>(e.filter));
  write_i32_le(buf, e.arg);
}

inline void write_string_filter_entry(std::vector<std::uint8_t>& buf, std::vector<std::string> const& literals,
                                      string_filter_entry const& e) {
  write_u8(buf, static_cast<std::uint8_t>(e.filter));
  write_i32_le(buf, e.arg1);
  write_i32_le(buf, e.arg2);
  auto it1 = std::find_if(literals.begin(), literals.end(), [&](auto const& s) { return s == e.str_arg1; });
  write_u64_le(buf, it1 != literals.end() ? static_cast<std::uint64_t>(std::distance(literals.begin(), it1)) : UINT64_MAX);
  auto it2 = std::find_if(literals.begin(), literals.end(), [&](auto const& s) { return s == e.str_arg2; });
  write_u64_le(buf, it2 != literals.end() ? static_cast<std::uint64_t>(std::distance(literals.begin(), it2)) : UINT64_MAX);
}

inline void write_var_ref(std::vector<std::uint8_t>& buf, std::vector<std::string> const& literals,
                          bc_var_ref const& ref) {
  write_string(buf, ref.key);
  write_u8(buf, ref.has_dot ? 1 : 0);
  write_u8(buf, ref.is_loop_parent ? 1 : 0);
  write_u8(buf, static_cast<std::uint8_t>(ref.compare_rhs_kind));
  write_string(buf, ref.compare_rhs_text);
  write_u8(buf, ref.compare_rhs_has_dot ? 1 : 0);
  write_u8(buf, ref.filter_flags);
  write_u8(buf, ref.section_op_count);
  for (std::uint8_t i = 0; i < ref.section_op_count; ++i) {
    write_u8(buf, static_cast<std::uint8_t>(ref.section_ops[i].kind));
    write_u32_le(buf, static_cast<std::uint32_t>(ref.section_ops[i].arg));
    write_u32_le(buf, static_cast<std::uint32_t>(ref.section_ops[i].arg2));
    auto it1 = std::find_if(literals.begin(), literals.end(),
                            [&](auto const& s) { return s == ref.section_ops[i].str_arg1; });
    write_u64_le(buf, it1 != literals.end()
                         ? static_cast<std::uint64_t>(std::distance(literals.begin(), it1))
                         : UINT64_MAX);
  }
  write_u64_le(buf, ref.filters.size());
  for (auto const& f : ref.filters) write_string_filter_entry(buf, literals, f);
  write_u64_le(buf, ref.int_filters.size());
  for (auto const& f : ref.int_filters) write_int_filter_entry(buf, f);
  write_u64_le(buf, ref.float_filters.size());
  for (auto const& f : ref.float_filters) write_float_filter_entry(buf, f);
  write_u8(buf, ref.root_fallback);
}

inline void write_instruction(std::vector<std::uint8_t>& buf, bc_instruction const& inst) {
  write_u8(buf, static_cast<std::uint8_t>(inst.op));
  write_u32_le(buf, inst.operand);
  write_u32_le(buf, inst.operand2);
  write_u32_le(buf, inst.operand3);
}

// 前方宣言（再帰的な partial 対応）
void write_bytecode(std::vector<std::uint8_t>& buf, bytecode const& bc);
void write_partial_entry(std::vector<std::uint8_t>& buf, partial_entry const& pe);

inline void write_partial_entry(std::vector<std::uint8_t>& buf, partial_entry const& pe) {
  write_string(buf, pe.name);
  write_u8(buf, pe.local ? 1 : 0);
  if (pe.bc) { write_bytecode(buf, *pe.bc); } else { bytecode empty; write_bytecode(buf, empty); }
}

inline void write_bytecode(std::vector<std::uint8_t>& buf, bytecode const& bc) {
  write_u8(buf, bc.is_simple ? 1 : 0);
  write_u64_le(buf, bc.literal_total_size);
  write_u64_le(buf, bc.instructions.size());
  for (auto const& inst : bc.instructions) write_instruction(buf, inst);
  write_u64_le(buf, bc.literals.size());
  for (auto const& lit : bc.literals) write_string(buf, lit);
  write_u64_le(buf, bc.var_refs.size());
  for (auto const& ref : bc.var_refs) write_var_ref(buf, bc.literals, ref);
  write_u64_le(buf, bc.partial_entries.size());
  for (auto const& pe : bc.partial_entries) write_partial_entry(buf, pe);
}

// ---- span ベース読み込みヘルパ (cursor、ストリーム不要) ----

/** @brief span ベース読み込み状態（カーソル + エラー + バージョン） */
struct span_cursor {
  std::span<std::uint8_t const> data;
  std::size_t pos = 0;
  bool ok = true;
  error_code ec = error_code::none;
  int version = 1;
};

/** @brief vector::reserve を例外安全に試行（span_cursor 版） */
template <class Vec>
inline bool try_reserve(Vec& v, std::size_t n, span_cursor& cur) noexcept {
#if INJAMM_HAS_EXCEPTIONS
  try {
    v.reserve(n);
    return true;
  } catch (...) {
    cur.ok = false;
    cur.ec = error_code::out_of_memory;
    return false;
  }
#else
  if (n > v.max_size()) { cur.ok = false; cur.ec = error_code::out_of_memory; return false; }
  v.reserve(n);
  return true;
#endif
}

/** @brief string::append を例外安全に試行 */
inline bool try_append(std::string& s, std::string_view sv, error_ctx* err) noexcept {
#if INJAMM_HAS_EXCEPTIONS
  try {
    s.append(sv);
    return true;
  } catch (...) {
    if (err != nullptr) {
      err->ec = error_code::out_of_memory;
    }
    return false;
  }
#else
  if (sv.size() > s.max_size() - s.size()) {
    if (err != nullptr) {
      err->ec = error_code::out_of_memory;
    }
    return false;
  }
  s.append(sv);
  return true;
#endif
}

/** @brief vector::push_back を例外安全に試行 */
template <class Vec, class Val>
inline bool try_push_back(Vec& v, Val const& val, error_ctx* err) noexcept {
#if INJAMM_HAS_EXCEPTIONS
  try {
    v.push_back(val);
    return true;
  } catch (...) {
    if (err != nullptr) {
      err->ec = error_code::out_of_memory;
    }
    return false;
  }
#else
  if (v.size() >= v.max_size()) {
    if (err != nullptr) {
      err->ec = error_code::out_of_memory;
    }
    return false;
  }
  v.push_back(val);
  return true;
#endif
}

inline std::uint8_t read_u8(span_cursor& cur) noexcept {
  if (!cur.ok || cur.pos >= cur.data.size()) { cur.ok = false; cur.ec = error_code::no_read_input; return 0; }
  return cur.data[cur.pos++];
}

inline std::uint32_t read_u32_le(span_cursor& cur) noexcept {
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(read_u8(cur)) << (i * 8);
  return v;
}

inline std::uint64_t read_u64_le(span_cursor& cur) noexcept {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(read_u8(cur)) << (i * 8);
  return v;
}

inline std::int32_t read_i32_le(span_cursor& cur) noexcept {
  return static_cast<std::int32_t>(read_u32_le(cur));
}

inline std::string read_string(span_cursor& cur) {
  auto len = read_u64_le(cur);
  if (!cur.ok) return {};
  constexpr std::uint64_t max_string_len = 16 * 1024 * 1024;
  if (len > max_string_len) { cur.ok = false; cur.ec = error_code::syntax_error; return {}; }
  if (cur.pos + static_cast<std::size_t>(len) > cur.data.size()) { cur.ok = false; cur.ec = error_code::no_read_input; return {}; }
  std::string s(reinterpret_cast<char const*>(cur.data.data() + cur.pos), static_cast<std::size_t>(len));
  cur.pos += static_cast<std::size_t>(len);
  return s;
}

inline bc_instruction read_instruction(span_cursor& cur) noexcept {
  bc_instruction inst;
  inst.op = static_cast<bc_opcode>(read_u8(cur));
  if (cur.ok && !is_valid_opcode(inst.op)) { cur.ok = false; cur.ec = error_code::syntax_error; }
  inst.operand  = read_u32_le(cur);
  inst.operand2 = read_u32_le(cur);
  inst.operand3 = read_u32_le(cur);
  return inst;
}

inline string_filter_entry read_string_filter_entry(span_cursor& cur, std::vector<std::string> const& literals) noexcept {
  string_filter_entry e;
  e.filter = static_cast<string_filter>(read_u8(cur));
  e.arg1 = read_i32_le(cur);
  e.arg2 = read_i32_le(cur);
  auto idx1 = read_u64_le(cur);
  auto idx2 = read_u64_le(cur);
  if (cur.ok) {
    if (idx1 < literals.size()) e.str_arg1 = literals[static_cast<std::size_t>(idx1)];
    if (idx2 < literals.size()) e.str_arg2 = literals[static_cast<std::size_t>(idx2)];
  }
  return e;
}

inline int_filter_entry read_int_filter_entry(span_cursor& cur) noexcept {
  int_filter_entry e;
  e.filter = static_cast<int_filter>(read_u8(cur));
  e.arg = read_i32_le(cur);
  return e;
}

inline float_filter_entry read_float_filter_entry(span_cursor& cur) noexcept {
  float_filter_entry e;
  e.filter = static_cast<float_filter>(read_u8(cur));
  e.arg = read_i32_le(cur);
  return e;
}

template <class T>
bytecode read_bytecode_body(span_cursor& cur, int depth = 0);

} // namespace detail

#ifndef INJAMM_NO_BYTECODE_IO
/** @brief バイトコードをストリームに保存（マジック + バージョン + 実データ） */
inline error_code save_bytecode(detail::bytecode const& bc, std::ostream& os) {
  constexpr char magic[] = {'I', 'J', 'B', 'C'};
  os.write(magic, 4);
  detail::write_u32_le(os, 6); // バージョン 6（ルート型フォールバックフラグ対応）
  if (!os) return error_code::no_read_input;

  detail::write_bytecode(os, bc);
  if (!os) return error_code::no_read_input;

  return error_code::none;
}

/**
 * @brief ストリームからバイトコードを読み込む
 *
 * @tparam T コンテキスト型（field_index 再解決に使用）
 * @param is 入力ストリーム
 * @return expected<detail::bytecode> 読み込まれたバイトコード、またはエラー
 */
template <class T>
expected<detail::bytecode> load_bytecode(std::istream& is) {
  detail::read_state state;

  // マジックチェック
  char magic[4]{};
  is.read(magic, 4);
  if (is.gcount() != 4)
    return std::unexpected(error_ctx{0, error_code::no_read_input, "Failed to read magic"});
  if (magic[0] != 'I' || magic[1] != 'J' || magic[2] != 'B' || magic[3] != 'C')
    return std::unexpected(error_ctx{0, error_code::syntax_error, "Invalid magic"});

  // バージョンチェック
  auto version = detail::read_u32_le(is, state);
  if (!state.ok)
    return std::unexpected(error_ctx{0, state.ec, "Failed to read version"});
  if (version == 1) {
    state.version = 1;
  } else if (version == 2) {
    state.version = 2;
  } else if (version == 3) {
    state.version = 3;
  } else if (version == 4) {
    state.version = 4;
  } else if (version == 5) {
    state.version = 5;
  } else if (version == 6) {
    state.version = 6;
  } else {
    return std::unexpected(error_ctx{0, error_code::type_mismatch, "Unsupported bytecode version"});
  }

  // 本体読み込み
  auto bc = detail::read_bytecode_body<T>(is, state);
  if (!state.ok)
    return std::unexpected(error_ctx{0, state.ec, "Failed to read bytecode"});

  return bc;
}
#endif // !INJAMM_NO_BYTECODE_IO

// ---- span ベース公開 API（常に有効） ----

/**
 * @brief バイトコードをバイト列（vector<uint8_t>）に保存する
 *
 * INJAMM_WASI_MINIMAL でも使用可能。出力バッファに追記する。
 * @param bc 保存するバイトコード
 * @param out 出力先バッファ（追記）
 * @return error_code エラーコード（現在は常に none）
 */
[[nodiscard]] inline error_code save_bytecode(detail::bytecode const& bc, std::vector<std::uint8_t>& out) {
  constexpr std::uint8_t magic[] = {'I', 'J', 'B', 'C'};
  out.insert(out.end(), magic, magic + 4);
  detail::write_u32_le(out, 6); // バージョン 6
  detail::write_bytecode(out, bc);
  return error_code::none;
}

/**
 * @brief バイト列（span<const uint8_t>）からバイトコードを読み込む
 *
 * INJAMM_WASI_MINIMAL でも使用可能。ゼロコピーで読み込む。
 * @tparam T コンテキスト型（field_index 再解決に使用）
 * @param buf 入力バイト列
 * @return expected<detail::bytecode> 読み込まれたバイトコード、またはエラー
 */
template <class T>
[[nodiscard]] expected<detail::bytecode> load_bytecode(std::span<std::uint8_t const> buf) {
  detail::span_cursor cur{buf, 0};

  // マジックチェック
  if (buf.size() < 8)
    return std::unexpected(error_ctx{0, error_code::no_read_input, "Buffer too small"});
  if (buf[0] != 'I' || buf[1] != 'J' || buf[2] != 'B' || buf[3] != 'C')
    return std::unexpected(error_ctx{0, error_code::syntax_error, "Invalid magic"});
  cur.pos = 4;

  // バージョンチェック
  auto version = detail::read_u32_le(cur);
  if (!cur.ok)
    return std::unexpected(error_ctx{0, cur.ec, "Failed to read version"});
  if (version >= 1 && version <= 6) {
    cur.version = static_cast<int>(version);
  } else {
    return std::unexpected(error_ctx{0, error_code::type_mismatch, "Unsupported bytecode version"});
  }

  auto bc = detail::read_bytecode_body<T>(cur);
  if (!cur.ok)
    return std::unexpected(error_ctx{0, cur.ec, "Failed to read bytecode"});
  return bc;
}

#ifndef INJAMM_NO_BYTECODE_IO
namespace detail {

/** @brief バイトコード本体をストリームから読み込み（命令列・リテラル・変数参照・partial） */
template <class T>
bytecode read_bytecode_body(std::istream& is, read_state& state, int depth) {
  if (depth > 64) { state.ok = false; state.ec = error_code::syntax_error; return {}; }
  bytecode bc;

  bc.is_simple = read_u8(is, state) != 0;
  bc.literal_total_size = read_u64_le(is, state);
  if (!state.ok) return bc;

  // 命令列
  auto ic = read_u64_le(is, state);
  if (!state.ok) return bc;
  constexpr std::uint64_t max_instructions = 1 << 20; // ~1M
  if (ic > max_instructions) { state.ok = false; state.ec = error_code::syntax_error; return bc; }
  if (!try_reserve(bc.instructions, static_cast<std::size_t>(ic), state)) return bc;
  for (std::uint64_t i = 0; i < ic; ++i) {
    if (!state.ok) break;
    bc.instructions.push_back(read_instruction(is, state));
  }
  if (!state.ok) return bc;

  // リテラルテーブル
  auto lc = read_u64_le(is, state);
  if (!state.ok) return bc;
  constexpr std::uint64_t max_literals = 1 << 20;
  if (lc > max_literals) { state.ok = false; state.ec = error_code::syntax_error; return bc; }
  if (!try_reserve(bc.literals, static_cast<std::size_t>(lc), state)) return bc;
  for (std::uint64_t i = 0; i < lc; ++i) {
    if (!state.ok) break;
    bc.literals.push_back(read_string(is, state));
  }
  if (!state.ok) return bc;

  // 変数参照テーブル
  auto vc = read_u64_le(is, state);
  if (!state.ok) return bc;
  constexpr std::uint64_t max_var_refs = 1 << 20;
  if (vc > max_var_refs) { state.ok = false; state.ec = error_code::syntax_error; return bc; }
  if (!try_reserve(bc.var_refs, static_cast<std::size_t>(vc), state)) return bc;
  for (std::uint64_t i = 0; i < vc; ++i) {
    if (!state.ok) break;
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
    if (state.version >= 3) {
      ref.section_op_count = read_u8(is, state);
      if (state.ok && ref.section_op_count > bc_var_ref::max_section_ops) { state.ok = false; state.ec = error_code::syntax_error; return bc; }
      for (std::uint8_t si = 0; si < ref.section_op_count && si < bc_var_ref::max_section_ops; ++si) {
        ref.section_ops[si].kind = static_cast<section_filter_op_kind>(read_u8(is, state));
        ref.section_ops[si].arg  = static_cast<std::int32_t>(read_u32_le(is, state));
        if (state.version >= 4)
          ref.section_ops[si].arg2 = static_cast<std::int32_t>(read_u32_le(is, state));
        if (state.version >= 5) {
          auto idx = read_u64_le(is, state);
          if (state.ok && idx < bc.literals.size()) {
            ref.section_ops[si].str_arg1 = bc.literals[static_cast<std::size_t>(idx)];
          }
        }
      }
    } else if (state.version >= 2) {
      auto sec_rev = read_u8(is, state) != 0;
      auto sec_take_raw = read_u32_le(is, state);
      if (sec_rev) {
        ref.section_ops[0] = {section_filter_op_kind::reverse, 0};
        ref.section_op_count = 1;
      }
      if (sec_take_raw > 0) {
        auto idx = ref.section_op_count;
        if (idx < bc_var_ref::max_section_ops) {
          ref.section_ops[idx] = {section_filter_op_kind::take, static_cast<std::int32_t>(sec_take_raw - 1u)};
          ref.section_op_count = idx + 1;
        }
      }
    }
    if (!state.ok) return bc;

    // 文字列フィルター
    auto fc = read_u64_le(is, state);
    if (!state.ok) return bc;
    if (fc > 64) { state.ok = false; state.ec = error_code::syntax_error; return bc; }
    if (!try_reserve(ref.filters, static_cast<std::size_t>(fc), state)) return bc;
    for (std::uint64_t j = 0; j < fc; ++j) {
      if (!state.ok) break;
      ref.filters.push_back(read_string_filter_entry(is, state, bc.literals));
    }
    if (!state.ok) return bc;

    // 整数フィルター
    auto ifc = read_u64_le(is, state);
    if (!state.ok) return bc;
    if (ifc > 64) { state.ok = false; state.ec = error_code::syntax_error; return bc; }
    if (!try_reserve(ref.int_filters, static_cast<std::size_t>(ifc), state)) return bc;
    for (std::uint64_t j = 0; j < ifc; ++j) {
      if (!state.ok) break;
      ref.int_filters.push_back(read_int_filter_entry(is, state));
    }
    if (!state.ok) return bc;

    // 浮動小数点フィルター
    auto ffc = read_u64_le(is, state);
    if (!state.ok) return bc;
    if (ffc > 64) { state.ok = false; state.ec = error_code::syntax_error; return bc; }
    if (!try_reserve(ref.float_filters, static_cast<std::size_t>(ffc), state)) return bc;
    for (std::uint64_t j = 0; j < ffc; ++j) {
      if (!state.ok) break;
      ref.float_filters.push_back(read_float_filter_entry(is, state));
    }
    if (!state.ok) return bc;

    /** v6: ルート型フォールバックフラグ（root_fb_*） */
    if (state.version >= 6) {
      ref.root_fallback = read_u8(is, state);
      if (!state.ok) return bc;
    }

    // コンテキスト型 T で field_index を再解決
    re_resolve_var_ref<T>(ref);

    bc.var_refs.push_back(std::move(ref));
  }

  // partial エントリ（再帰的に読み込み）
  auto pc = read_u64_le(is, state);
  if (!state.ok) return bc;
  constexpr std::uint64_t max_partials = 1024;
  if (pc > max_partials) { state.ok = false; state.ec = error_code::syntax_error; return bc; }
  if (!try_reserve(bc.partial_entries, static_cast<std::size_t>(pc), state)) return bc;
  for (std::uint64_t i = 0; i < pc; ++i) {
    if (!state.ok) break;
    auto name = read_string(is, state);
    if (!state.ok) break;
    auto local = read_u8(is, state) != 0;
    if (!state.ok) break;
    auto partial_bc = std::make_shared<bytecode>(read_bytecode_body<T>(is, state, depth + 1));
    if (!state.ok) break;
    bc.partial_entries.push_back(partial_entry{std::move(name), std::move(partial_bc), local});
  }
  if (!state.ok) return bc;

  if (state.ok) {
    auto const n_lit = bc.literals.size();
    auto const n_var = bc.var_refs.size();
    auto const n_ins = bc.instructions.size();
    auto const n_par = bc.partial_entries.size();
    for (auto const& inst : bc.instructions) {
      bool bad = false;
      switch (inst.op) {
        case bc_opcode::emit_literal:
          bad = inst.operand >= n_lit;
          break;
        case bc_opcode::emit_litvar:
        case bc_opcode::emit_litvar_raw:
          bad = inst.operand >= n_lit || inst.operand2 >= n_var;
          break;
        case bc_opcode::emit_var:
        case bc_opcode::emit_var_raw:
        case bc_opcode::emit_var_size:
        case bc_opcode::emit_at_root_field:
        case bc_opcode::emit_at_root_field_raw:
          bad = inst.operand >= n_var;
          break;
        case bc_opcode::emit_section:
        case bc_opcode::emit_inverted: {
          if (inst.operand2 >= n_var) { bad = true; break; }
          if (inst.operand == 0 || inst.operand > n_ins) bad = true;
          else if (inst.operand3 != 0 && inst.operand3 > n_ins) bad = true;
          break;
        }
        case bc_opcode::emit_if:
        case bc_opcode::emit_if_eq:
        case bc_opcode::emit_if_ne:
        case bc_opcode::emit_if_gt:
        case bc_opcode::emit_if_gte:
        case bc_opcode::emit_if_lt:
        case bc_opcode::emit_if_lte:
        case bc_opcode::emit_if_or:
        case bc_opcode::emit_if_and:
        case bc_opcode::emit_if_not:
        case bc_opcode::emit_if_filtered:
          if (inst.operand2 >= n_var) { bad = true; break; }
          if (inst.operand == 0 || inst.operand > n_ins) bad = true;
          if ((inst.op == bc_opcode::emit_if_or || inst.op == bc_opcode::emit_if_and) && inst.operand3 >= n_var) bad = true;
          break;
        case bc_opcode::resolve_filtered:
          if (inst.operand2 >= n_var) bad = true;
          break;
        case bc_opcode::call_partial:
          bad = inst.operand >= n_par;
          break;
        case bc_opcode::emit_else:
        case bc_opcode::emit_endif:
        case bc_opcode::emit_end:
          if (inst.operand != 0 && inst.operand > n_ins) bad = true;
          break;
        case bc_opcode::emit_at_section:
        case bc_opcode::emit_at_inverted:
          if (inst.operand == 0 || inst.operand > n_ins) bad = true;
          else if (inst.operand2 > 2) bad = true;
          break;
        case bc_opcode::filter_string:
        case bc_opcode::filter_int:
        case bc_opcode::filter_float:
          if (inst.operand2 > 64) bad = true;
          break;
        case bc_opcode::emit_filtered:
        case bc_opcode::emit_filtered_raw:
          break;
        default:
          break;
      }
      if (bad) { state.ok = false; state.ec = error_code::syntax_error; break; }
    }
  }

  return bc;
}

} // namespace detail
#endif // !INJAMM_NO_BYTECODE_IO

namespace detail {

/** @brief span ベースのバイトコード本体読み込み（命令列・リテラル・変数参照・partial） */
template <class T>
bytecode read_bytecode_body(span_cursor& cur, int depth) {
  if (depth > 64) { cur.ok = false; cur.ec = error_code::syntax_error; return {}; }
  bytecode bc;

  bc.is_simple = read_u8(cur) != 0;
  bc.literal_total_size = read_u64_le(cur);
  if (!cur.ok) return bc;

  // 命令列
  auto ic = read_u64_le(cur);
  if (!cur.ok) return bc;
  constexpr std::uint64_t max_instructions = 1 << 20;
  if (ic > max_instructions) { cur.ok = false; cur.ec = error_code::syntax_error; return bc; }
  if (!try_reserve(bc.instructions, static_cast<std::size_t>(ic), cur)) return bc;
  for (std::uint64_t i = 0; i < ic; ++i) {
    if (!cur.ok) break;
    bc.instructions.push_back(read_instruction(cur));
  }
  if (!cur.ok) return bc;

  // リテラルテーブル
  auto lc = read_u64_le(cur);
  if (!cur.ok) return bc;
  constexpr std::uint64_t max_literals = 1 << 20;
  if (lc > max_literals) { cur.ok = false; cur.ec = error_code::syntax_error; return bc; }
  if (!try_reserve(bc.literals, static_cast<std::size_t>(lc), cur)) return bc;
  for (std::uint64_t i = 0; i < lc; ++i) {
    if (!cur.ok) break;
    bc.literals.push_back(read_string(cur));
  }
  if (!cur.ok) return bc;

  // 変数参照テーブル
  auto vc = read_u64_le(cur);
  if (!cur.ok) return bc;
  constexpr std::uint64_t max_var_refs = 1 << 20;
  if (vc > max_var_refs) { cur.ok = false; cur.ec = error_code::syntax_error; return bc; }
  if (!try_reserve(bc.var_refs, static_cast<std::size_t>(vc), cur)) return bc;
  for (std::uint64_t i = 0; i < vc; ++i) {
    if (!cur.ok) break;
    auto key          = read_string(cur);
    auto has_dot      = read_u8(cur) != 0;
    auto is_loop_par  = read_u8(cur) != 0;
    auto cmp_kind     = static_cast<compare_operand_kind>(read_u8(cur));
    auto cmp_text     = read_string(cur);
    auto cmp_has_dot  = read_u8(cur) != 0;
    auto filter_flags = read_u8(cur);
    if (!cur.ok) return bc;

    bc_var_ref ref;
    ref.key = std::move(key);
    ref.has_dot = has_dot;
    ref.is_loop_parent = is_loop_par;
    ref.compare_rhs_kind = cmp_kind;
    ref.compare_rhs_text = std::move(cmp_text);
    ref.compare_rhs_has_dot = cmp_has_dot;
    ref.filter_flags = filter_flags;
    if (cur.version >= 3) {
      ref.section_op_count = read_u8(cur);
      if (cur.ok && ref.section_op_count > bc_var_ref::max_section_ops) { cur.ok = false; cur.ec = error_code::syntax_error; return bc; }
      for (std::uint8_t si = 0; si < ref.section_op_count && si < bc_var_ref::max_section_ops; ++si) {
        ref.section_ops[si].kind = static_cast<section_filter_op_kind>(read_u8(cur));
        ref.section_ops[si].arg  = static_cast<std::int32_t>(read_u32_le(cur));
        if (cur.version >= 4)
          ref.section_ops[si].arg2 = static_cast<std::int32_t>(read_u32_le(cur));
        if (cur.version >= 5) {
          auto idx = read_u64_le(cur);
          if (cur.ok && idx < bc.literals.size())
            ref.section_ops[si].str_arg1 = bc.literals[static_cast<std::size_t>(idx)];
        }
      }
    } else if (cur.version >= 2) {
      auto sec_rev     = read_u8(cur) != 0;
      auto sec_take    = read_u32_le(cur);
      if (sec_rev) { ref.section_ops[0] = {section_filter_op_kind::reverse, 0}; ref.section_op_count = 1; }
      if (sec_take > 0) {
        auto idx = ref.section_op_count;
        if (idx < bc_var_ref::max_section_ops) {
          ref.section_ops[idx] = {section_filter_op_kind::take, static_cast<std::int32_t>(sec_take - 1u)};
          ref.section_op_count = idx + 1;
        }
      }
    }
    if (!cur.ok) return bc;

    // 文字列フィルター
    auto fc = read_u64_le(cur);
    if (!cur.ok) return bc;
    if (fc > 64) { cur.ok = false; cur.ec = error_code::syntax_error; return bc; }
    if (!try_reserve(ref.filters, static_cast<std::size_t>(fc), cur)) return bc;
    for (std::uint64_t j = 0; j < fc; ++j) {
      if (!cur.ok) break;
      ref.filters.push_back(read_string_filter_entry(cur, bc.literals));
    }
    if (!cur.ok) return bc;

    // 整数フィルター
    auto ifc = read_u64_le(cur);
    if (!cur.ok) return bc;
    if (ifc > 64) { cur.ok = false; cur.ec = error_code::syntax_error; return bc; }
    if (!try_reserve(ref.int_filters, static_cast<std::size_t>(ifc), cur)) return bc;
    for (std::uint64_t j = 0; j < ifc; ++j) {
      if (!cur.ok) break;
      ref.int_filters.push_back(read_int_filter_entry(cur));
    }
    if (!cur.ok) return bc;

    // 浮動小数点フィルター
    auto ffc = read_u64_le(cur);
    if (!cur.ok) return bc;
    if (ffc > 64) { cur.ok = false; cur.ec = error_code::syntax_error; return bc; }
    if (!try_reserve(ref.float_filters, static_cast<std::size_t>(ffc), cur)) return bc;
    for (std::uint64_t j = 0; j < ffc; ++j) {
      if (!cur.ok) break;
      ref.float_filters.push_back(read_float_filter_entry(cur));
    }
    if (!cur.ok) return bc;

    if (cur.version >= 6) {
      ref.root_fallback = read_u8(cur);
      if (!cur.ok) return bc;
    }

    re_resolve_var_ref<T>(ref);
    bc.var_refs.push_back(std::move(ref));
  }

  // partial エントリ（再帰的に読み込み）
  auto pc = read_u64_le(cur);
  if (!cur.ok) return bc;
  constexpr std::uint64_t max_partials = 1024;
  if (pc > max_partials) { cur.ok = false; cur.ec = error_code::syntax_error; return bc; }
  if (!try_reserve(bc.partial_entries, static_cast<std::size_t>(pc), cur)) return bc;
  for (std::uint64_t i = 0; i < pc; ++i) {
    if (!cur.ok) break;
    auto name  = read_string(cur);
    if (!cur.ok) break;
    auto local = read_u8(cur) != 0;
    if (!cur.ok) break;
    auto partial_bc = std::make_shared<bytecode>(read_bytecode_body<T>(cur, depth + 1));
    if (!cur.ok) break;
    bc.partial_entries.push_back(partial_entry{std::move(name), std::move(partial_bc), local});
  }
  if (!cur.ok) return bc;

  // 命令列境界チェック（stream 版と共通ロジック）
  if (cur.ok) {
    auto const n_lit = bc.literals.size();
    auto const n_var = bc.var_refs.size();
    auto const n_ins = bc.instructions.size();
    auto const n_par = bc.partial_entries.size();
    for (auto const& inst : bc.instructions) {
      bool bad = false;
      switch (inst.op) {
        case bc_opcode::emit_literal:
          bad = inst.operand >= n_lit; break;
        case bc_opcode::emit_litvar:
        case bc_opcode::emit_litvar_raw:
          bad = inst.operand >= n_lit || inst.operand2 >= n_var; break;
        case bc_opcode::emit_var:
        case bc_opcode::emit_var_raw:
        case bc_opcode::emit_var_size:
        case bc_opcode::emit_at_root_field:
        case bc_opcode::emit_at_root_field_raw:
          bad = inst.operand >= n_var; break;
        case bc_opcode::emit_section:
        case bc_opcode::emit_inverted: {
          if (inst.operand2 >= n_var) { bad = true; break; }
          if (inst.operand == 0 || inst.operand > n_ins) bad = true;
          else if (inst.operand3 != 0 && inst.operand3 > n_ins) bad = true;
          break;
        }
        case bc_opcode::emit_if:
        case bc_opcode::emit_if_eq:
        case bc_opcode::emit_if_ne:
        case bc_opcode::emit_if_gt:
        case bc_opcode::emit_if_gte:
        case bc_opcode::emit_if_lt:
        case bc_opcode::emit_if_lte:
        case bc_opcode::emit_if_or:
        case bc_opcode::emit_if_and:
        case bc_opcode::emit_if_not:
        case bc_opcode::emit_if_filtered:
          if (inst.operand2 >= n_var) { bad = true; break; }
          if (inst.operand == 0 || inst.operand > n_ins) bad = true;
          if ((inst.op == bc_opcode::emit_if_or || inst.op == bc_opcode::emit_if_and) && inst.operand3 >= n_var) bad = true;
          break;
        case bc_opcode::resolve_filtered:
          if (inst.operand2 >= n_var) bad = true; break;
        case bc_opcode::call_partial:
          bad = inst.operand >= n_par; break;
        case bc_opcode::emit_else:
        case bc_opcode::emit_endif:
        case bc_opcode::emit_end:
          if (inst.operand != 0 && inst.operand > n_ins) bad = true; break;
        case bc_opcode::emit_at_section:
        case bc_opcode::emit_at_inverted:
          if (inst.operand == 0 || inst.operand > n_ins) bad = true;
          else if (inst.operand2 > 2) bad = true;
          break;
        case bc_opcode::filter_string:
        case bc_opcode::filter_int:
        case bc_opcode::filter_float:
          if (inst.operand2 > 64) bad = true; break;
        case bc_opcode::emit_filtered:
        case bc_opcode::emit_filtered_raw:
          break;
        default:
          break;
      }
      if (bad) { cur.ok = false; cur.ec = error_code::syntax_error; break; }
    }
  }

  return bc;
}

} // namespace detail
} // namespace injamm
