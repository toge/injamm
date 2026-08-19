#pragma once

/**
 * @file ct_exec.hpp
 * @brief NTTP コンパイル時バイトコードのアンロール実行器（staged interpreter）
 *
 * @details 2 つの高速パスを提供する:
 *
 *  **1. フルアンロール（ct_executor）**:
 *     単純テンプレート（emit_litvar / emit_literal / emit_var / halt のみ、
 *     ドットパス・セクション・フィルタなし）では全命令をコンパイル時に展開する。
 *
 *  **2. ハイブリッドアンロール（ct_hybrid_executor）**:
 *     テンプレートが「直線コード + セクション/条件分岐」の組み合わせの場合、
 *     直線区間だけをコンパイル時に展開し、ブロック（セクション/条件）は
 *     ランタイム VM（bc_executor::execute_impl）に委譲する。
 *     ブロック内は VM が処理し、ブロック間の直線コードはアンロールされる。
 *
 *  値のシリアライズは bc_executor::emit_value_static を再利用するため
 *  ランタイム VM と出力が完全に一致する。
 */

#include "bytecode.hpp"
#include "bytecode_ct_compile.hpp"
#include "bytecode_exec.hpp"
#include "glz_dispatch.hpp"
#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace injamm::detail {

template <typename...>
inline constexpr bool ct_exec_dependent_false = false;

// ============================================================================
//  opcode 分類
// ============================================================================

constexpr bool ct_is_straight_op(bc_opcode op) {
  switch (op) {
    case bc_opcode::emit_literal:
    case bc_opcode::emit_var:
    case bc_opcode::emit_var_raw:
    case bc_opcode::emit_litvar:
    case bc_opcode::emit_litvar_raw:
    case bc_opcode::halt:
      return true;
    default:
      return false;
  }
}

/** @brief ブロック開始命令（セクション/条件分岐）。span 区間の委譲対象。 */
constexpr bool ct_is_block_start_op(bc_opcode op) {
  switch (op) {
    case bc_opcode::emit_section:
    case bc_opcode::emit_inverted:
    case bc_opcode::emit_if:
    case bc_opcode::emit_if_eq:
    case bc_opcode::emit_if_ne:
    case bc_opcode::emit_if_gt:
    case bc_opcode::emit_if_gte:
    case bc_opcode::emit_if_lt:
    case bc_opcode::emit_if_lte:
    case bc_opcode::emit_if_not:
    case bc_opcode::emit_if_or:
    case bc_opcode::emit_if_and:
    case bc_opcode::emit_if_filtered:
    case bc_opcode::emit_at_section:
    case bc_opcode::emit_at_inverted:
      return true;
    default:
      return false;
  }
}

// ============================================================================
//  ブロック末尾の静的探索（ct_executor と ct_hybrid_executor の共通基盤）
// ============================================================================

/**
 * @brief ブロック開始命令の末尾（ terminator の次）を静的探索する
 * @param bc    コンパイル時バイトコード
 * @param start ブロック開始命令のインデックス
 * @return ブロック末尾のインデックス（ terminator の次）。不正な構造の場合は bc.instr_count
 */
template <std::size_t N>
constexpr std::size_t ct_span_end(ct_bytecode<N> const& bc, std::size_t start) {
  std::size_t depth = 0;
  for (std::size_t i = start; i < bc.instr_count; ++i) {
    auto op = bc.instructions[i].op;
    if (ct_is_block_start_op(op)) {
      ++depth;
    } else if (op == bc_opcode::emit_end || op == bc_opcode::emit_endif) {
      if (depth == 0)
        return i + 1;
      --depth;
    }
  }
  return bc.instr_count;
}

// ============================================================================
//  フルアンロール: ct_executor（従来どおり）
// ============================================================================

/** @brief コンパイル時アンロール実行が可能な単純テンプレートか判定する */
template <std::size_t N>
constexpr bool ct_is_unrollable(ct_bytecode<N> const& bc) {
  for (std::size_t i = 0; i < bc.instr_count; ++i) {
    auto const& instr = bc.instructions[i];
    if (!ct_is_straight_op(instr.op))
      return false;
    // 変数参照の解析（直線命令のみ）
    if (instr.op != bc_opcode::emit_literal && instr.op != bc_opcode::halt) {
      auto var_ref_idx = (instr.op == bc_opcode::emit_var || instr.op == bc_opcode::emit_var_raw) ? instr.operand : instr.operand2;
      auto const& ref = bc.var_refs[var_ref_idx];
      if (ref.field_index == UINT32_MAX || ref.key.size == 0)
        return false;
      for (std::size_t k = 0; k < ref.key.size; ++k)
        if (ref.key.data[k] == '.')
          return false;
      if (ref.section_op_count != 0)
        return false;
    }
  }
  return true;
}

/** @brief リテラル文字列を取得する（インデックスはコンパイル時定数） */
template <typename Data, std::uint32_t LitIdx>
constexpr std::string_view ct_literal() {
  auto const& e = Data::ct_bc.lit_entries[LitIdx];
  return {Data::ct_bc.string_pool.data() + e.offset, e.size};
}

/** @brief 変数値を出力する（フィールドインデックスはコンパイル時定数） */
template <typename Data, typename T, std::uint32_t VarRefIdx>
void ct_emit_var(T const& value, std::string& out, bool raw) {
  constexpr std::size_t field_index = static_cast<std::size_t>(Data::ct_bc.var_refs[VarRefIdx].field_index);
  auto                  tied        = glz::to_tie(value);
  auto const&           field       = glz::get<field_index>(tied);
  bc_executor<T>::emit_value_static(out, field, raw);
}

/** @brief 単一の直線命令をコンパイル時に展開する。戻り true = halt に到達 */
template <typename Data, typename T, std::size_t I>
constexpr bool ct_emit_straight(T const& value, std::string& out) {
  static constexpr auto&     instr = Data::ct_bc.instructions[I];
  static constexpr bc_opcode op    = instr.op;
  if constexpr (op == bc_opcode::emit_literal) {
    out.append(ct_literal<Data, instr.operand>());
    return false;
  } else if constexpr (op == bc_opcode::emit_var || op == bc_opcode::emit_var_raw) {
    ct_emit_var<Data, T, instr.operand>(value, out, op == bc_opcode::emit_var_raw);
    return false;
  } else if constexpr (op == bc_opcode::emit_litvar || op == bc_opcode::emit_litvar_raw) {
    out.append(ct_literal<Data, instr.operand>());
    ct_emit_var<Data, T, instr.operand2>(value, out, op == bc_opcode::emit_litvar_raw);
    return false;
  } else if constexpr (op == bc_opcode::halt) {
    return true;
  } else {
    static_assert(ct_exec_dependent_false<T>, "ct_emit_straight: unsupported opcode");
  }
}

/** @brief 単一命令のコンパイル時実行（ct_executor 用） */
template <typename Data, typename T, std::size_t I>
struct ct_exec_one {
  static void run(T const& value, std::string& out) { (void)ct_emit_straight<Data, T, I>(value, out); }
};

/** @brief コンパイル時アンロール実行器（単純テンプレート専用） */
template <typename Data, typename T>
struct ct_executor {
  static expected<std::string> run(T const& value) {
    std::string out;
    out.reserve(estimate());
    run_into(value, out);
    return out;
  }

  static void run_into(T const& value, std::string& out) {
    out.clear();
    if (out.capacity() < estimate())
      out.reserve(estimate());
    exec_seq(value, out, std::make_index_sequence<Data::ct_bc.instr_count>{});
  }

  static constexpr std::size_t estimate() {
    std::size_t lit_total = 0;
    for (std::size_t i = 0; i < Data::ct_bc.literal_count; ++i)
      lit_total += Data::ct_bc.lit_entries[i].size;
    std::size_t est = lit_total * 4 + Data::ct_bc.var_ref_count * 32;
    return est < 256 ? 256 : est;
  }

private:
  template <std::size_t... I>
  static void exec_seq(T const& value, std::string& out, std::index_sequence<I...>) {
    (ct_exec_one<Data, T, I>::run(value, out), ...);
  }
};

// ============================================================================
//  ハイブリッドアンロール: ct_hybrid_executor（セクション/条件含むテンプレート用）
// ============================================================================

/** @brief テンプレートがハイブリッド実行に適しているか判定する
 *
 *  すべての命令が「直線命令」または「ブロック開始命令」で構成されていれば true。
 *  ブロック内部の命令も含めて走査する（call_partial / resolve_filtered / @変数等の
 *  委譲先ではない命令がどこにでも出現した場合は false → ランタイム VM にフォールバック）。
 *  ブロック末尾の well-formed 性も検証する。
 */
template <std::size_t N>
constexpr bool ct_is_hybrid_eligible(ct_bytecode<N> const& bc) {
  for (std::size_t i = 0; i < bc.instr_count; ++i) {
    auto op = bc.instructions[i].op;
    if (ct_is_straight_op(op)) {
      if (op != bc_opcode::emit_literal && op != bc_opcode::halt) {
        auto var_ref_idx = (op == bc_opcode::emit_var || op == bc_opcode::emit_var_raw) ? bc.instructions[i].operand : bc.instructions[i].operand2;
        auto const& ref  = bc.var_refs[var_ref_idx];
        if (ref.field_index == UINT32_MAX || ref.key.size == 0)
          return false;
        for (std::size_t k = 0; k < ref.key.size; ++k)
          if (ref.key.data[k] == '.')
            return false;
        if (ref.section_op_count != 0)
          return false;
      }
    } else if (ct_is_block_start_op(op)) {
      auto E = ct_span_end(bc, i);
      if (E <= i + 1 || E > bc.instr_count)
        return false;
    } else {
      return false;
    }
  }
  return true;
}

/** @brief ハイブリッド実行の 1 ステップ
 *
 *  直線命令 → インライン展開、ブロック開始命令 → ランタイム VM に委譲。
 *  再帰深度 = ブロック間の直線命令数 + ブロック数（バイトコードの上位レベル命令数）。
 */
template <typename Data, typename T, std::size_t PC, std::size_t END>
struct ct_hybrid_step {
  static expected<void> run(T const& value, std::string& out, bc_executor<T>& exec) {
    if constexpr (PC >= END) {
      return {};
    } else {
      static constexpr auto&     instr = Data::ct_bc.instructions[PC];
      static constexpr bc_opcode op    = instr.op;
      if constexpr (ct_is_straight_op(op)) {
        if (ct_emit_straight<Data, T, PC>(value, out))
          return {};
        return ct_hybrid_step<Data, T, PC + 1, END>::run(value, out, exec);
      } else if constexpr (ct_is_block_start_op(op)) {
        // ブロック全体をランタイム VM に委譲（セクション/条件分岐/ネストを VM が処理）
        static constexpr std::size_t span_end = ct_span_end(Data::ct_bc, PC);
        auto                         r        = exec.execute_impl(PC, span_end);
        if (!r)
          return r;
        return ct_hybrid_step<Data, T, span_end, END>::run(value, out, exec);
      } else {
        static_assert(ct_exec_dependent_false<T>, "ct_hybrid_step: unexpected opcode");
      }
    }
  }
};

/** @brief ハイブリッドアンロール実行器
 *
 *  ブロック開始命令（セクション/条件分岐）が含まれるテンプレートを処理する。
 *  直線区間はコンパイル時にアンロール、ブロック区間はランタイム VM に委譲する。
 *  ランタイム VM の execute_impl(start, end) でブロックを実行し、
 *  ブロック処理後はハイブリッドが再開して次の直線区間をアンロールする。
 *
 *  @note 呼び出し側が bc_executor を構築して渡す（escape_hatch.hpp で nttp_bytecode_holder
 *        から構築）。これにより ct_exec.hpp は escape_hatch.hpp に依存しない。
 */
template <typename Data, typename T>
struct ct_hybrid_executor {
  /** @brief out は内容がクリアされる。exec.out_ は out に束縛されていること。 */
  static expected<void> run_into(T const& value, std::string& out, bc_executor<T>& exec) {
    out.clear();
    if (out.capacity() < ct_executor<Data, T>::estimate())
      out.reserve(ct_executor<Data, T>::estimate());
    auto r = ct_hybrid_step<Data, T, 0, Data::ct_bc.instr_count>::run(value, out, exec);
    if (!r)
      return r;
    return {};
  }
};

} // namespace injamm::detail
