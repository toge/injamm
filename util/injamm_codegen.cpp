/**
 * @file injamm_codegen.cpp
 * @brief バイトコード(.bc)またはテンプレート文字列から C++ レンダリング関数を生成する
 *
 * @details このツールはバイトコードファイルまたはテンプレート文字列を元に、
 *          glaze に依存しない C++ テンプレート関数を生成する。
 *          生成された関数は直接フィールドアクセスにより高速にレンダリングを行う。
 *          テンプレート文字列から生成する場合は、内部で engine によるコンパイルと
 *          バイトコードシリアライズを経由して既存のコードジェネレータに受け渡す。
 *
 *          用法: injamm_codegen (-i <input.bc> | -e <template>) -t <Type> -o <output.hpp>
 *
 * @note 生成されるコードは以下の機能に対応している:
 *       - 変数展開 ({/{{var}}})
 *       - HTML エスケープ ({/{{var}}} vs {{{{var}}}})
 *       - セクション ({/{#section}...{/section}})
 *       - 条件分岐 ({/{#if cond}...{else}...{/if}})
 *       - 比較演算 ({/{#if x > 100}})
 *       - フィルタ ({/{var|upper}}, {/{var|lower}}, etc.)
 *       - ループ変数 ({/{@index}}, {/{@first}}, {/{@last}})
 */

#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <injamm/bytecode_fwd.hpp>

/**
 * @brief -e オプションによるテンプレート文字列からの直接コード生成のために必要
 * @details engine<DummyCtx> によるテンプレートコンパイルおよび save_bytecode による
 *          バイトコードシリアライズに必要な関数・型を提供する。
 *          従来の -i オプション（.bc ファイル入力）では不要だが、単一バイナリとして
 *          両方のパスをサポートするためにインクルードする。
 */
#include "injamm.hpp"

// ============================================================
// バイトコード定義（glaze 非依存の最小限の型）
// ============================================================

namespace bc {

using opcode = injamm::detail::bc_opcode;

/**
 * @brief 中間命令
 * @details オペコードと最大 3 つのオペランドからなる中間表現。
 *          operand: リテラルインデックスまたはジャンプ先オフセット
 *          operand2: 変数参照インデックス
 *          operand3: else_target（else 本体開始、0 = なし）
 */
struct instruction {
  opcode op;              /**< オペコード */
  std::uint32_t operand = 0;   /**< オペランド 1 */
  std::uint32_t operand2 = 0;  /**< オペランド 2（変数参照インデックス） */
  std::uint32_t operand3 = 0;  /**< オペランド 3（ジャンプ先） */
};

/** @brief 文字列フィルタエントリ */
struct string_filter_entry {
  std::uint8_t filter;    /**< フィルタの種別 */
  std::int32_t arg1 = 0;  /**< 第 1 引数 */
  std::int32_t arg2 = 0;  /**< 第 2 引数 */
  std::string str_arg1;   /**< 文字列引数 1 */
  std::string str_arg2;   /**< 文字列引数 2 */
};

/** @brief 整数フィルタエントリ */
struct int_filter_entry {
  std::uint8_t filter;    /**< フィルタの種別 */
  std::int32_t arg = 0;   /**< 引数 */
};

/** @brief 実数フィルタエントリ */
struct float_filter_entry {
  std::uint8_t filter;    /**< フィルタの種別 */
  std::int32_t arg = 0;   /**< 引数 */
};

/**
 * @brief 変数参照テーブルエントリ
 * @details テンプレート内の変数参照を表す。ドット区切りパス、
 *          フィルタチェーン、比較演算情報を保持する。
 */
struct var_ref {
  std::string key;                        /**< 変数名 */
  bool has_dot = false;                   /**< ドット区切りパス（ネスト）を持つか */
  bool is_loop_parent = false;            /**< loop.parent. 始まりか */
  std::uint8_t root_fallback = injamm::detail::root_fb_none; /**< ルート型フォールバックモード（v6） */
  std::uint8_t compare_rhs_kind = 0;      /**< 比較の右オペランド種別 */
  std::string compare_rhs_text;           /**< 右オペランド文字列 */
  bool compare_rhs_has_dot = false;       /**< 右オペランドがドット区切りパスか */
  std::uint8_t filter_flags = 0;          /**< フィルタ特殊フラグ */
  /** @brief セクションフィルタパイプライン */
  struct section_op {
    injamm::detail::section_filter_op_kind kind = injamm::detail::section_filter_op_kind::reverse;
    std::int32_t arg = 0;   /**< take/skip/take_last/skip_last の引数、stride の取得数 */
    std::int32_t arg2 = 0;  /**< stride のスキップ数 */
    std::string_view str_arg1; /**< join の separator 文字列 */
  };
  static constexpr std::uint8_t max_section_ops = 4;
  std::array<section_op, max_section_ops> section_ops{};
  std::uint8_t section_op_count = 0;
  std::vector<string_filter_entry> filters;     /**< 文字列フィルタチェーン */
  std::vector<int_filter_entry> int_filters;    /**< 整数フィルタチェーン */
  std::vector<float_filter_entry> float_filters; /**< 実数フィルタチェーン */
};

/** @brief 名前付き partial エントリ */
struct partial_entry {
  std::string name;           /**< partial 名 */
  bool local = false;         /**< local partial の場合 true */
  struct bytecode* bc = nullptr; /**< プリコンパイル済みバイトコード */
};

/**
 * @brief コンパイル済みバイトコード
 * @details 命令列、リテラルテーブル、変数参照テーブルを保持する。
 */
struct bytecode {
  bool is_simple = false;             /**< 単純テンプレートフラグ */
  std::uint64_t literal_total_size = 0; /**< 全リテラルの合計サイズ */
  std::vector<instruction> instructions;  /**< 命令列 */
  std::vector<std::string> literals;      /**< リテラル文字列テーブル */
  std::vector<var_ref> var_refs;          /**< 変数参照テーブル */
  std::vector<partial_entry> partial_entries; /**< partial エントリ */
};

} // namespace bc

// ============================================================
// バイナリパーサー
// ============================================================

/**
 * @brief バイトコードのバイナリ形式を読み込むパーサー
 * @details injamm::detail::read_bytecode_body と同一のフォーマットを
 *          glaze に依存せずに読み込む。リトルエンディアン形式。
 */
class reader {
  const char* data_;  /**< 入力バッファへのポインタ */
  std::size_t pos_ = 0;  /**< 現在の読み込み位置 */
  std::size_t size_;  /**< バッファサイズ */
  int version_ = 1;  /**< バイトコードバージョン（v2 以降はセクションフィルタを読み込む） */
  bool failed_ = false;  /**< 境界違反・上限超過などで読み込みを中断した */
  static constexpr std::uint64_t max_string_len = 16 * 1024 * 1024;
  static constexpr std::uint64_t max_instructions = 1 << 20;
  static constexpr std::uint64_t max_literals = 1 << 20;
  static constexpr std::uint64_t max_var_refs = 1 << 20;
  static constexpr std::uint64_t max_partials = 1024;
  static constexpr std::uint64_t max_filters = 64;
  static constexpr int max_partial_depth = 64;

public:
  /**
   * @brief コンストラクタ
   * @param data 入力バッファ
   * @param size バッファサイズ（バイト）
   */
  reader(const char* data, std::size_t size) : data_(data), size_(size) {}

  /** @brief 読み込み状態が正常か */
  bool ok() const { return !failed_ && pos_ <= size_; }
  /** @brief 現在の読み込み位置 */
  std::size_t pos() const { return pos_; }

  /** @brief 読み込み失敗を記録する */
  void fail() { failed_ = true; }

  /** @brief 残りバイト数 */
  std::size_t remaining() const { return pos_ <= size_ ? size_ - pos_ : 0; }

  /** @brief 1 バイト読み込み */
  std::uint8_t read_u8() {
    if (remaining() < 1) { fail(); return 0; }
    return static_cast<std::uint8_t>(data_[pos_++]);
  }

  /** @brief 32 ビット符号なし整数をリトルエンディアンで読み込み */
  std::uint32_t read_u32_le() {
    if (remaining() < 4) { fail(); return 0; }
    std::uint32_t v = 0;
    std::memcpy(&v, data_ + pos_, 4);
    pos_ += 4;
    return v;
  }

  /** @brief 64 ビット符号なし整数をリトルエンディアンで読み込み */
  std::uint64_t read_u64_le() {
    if (remaining() < 8) { fail(); return 0; }
    std::uint64_t v = 0;
    std::memcpy(&v, data_ + pos_, 8);
    pos_ += 8;
    return v;
  }

  /** @brief 32 ビット符号あり整数をリトルエンディアンで読み込み */
  std::int32_t read_i32_le() {
    return static_cast<std::int32_t>(read_u32_le());
  }

  /** @brief 文字列を長さ前置形式で読み込み（サイズ(64bit) + 実データ） */
  std::string read_string() {
    auto len = read_u64_le();
    // 16MiB 上限 + 減算によるオーバーフロー回避
    if (failed_ || len > max_string_len || len > remaining()) {
      fail();
      return {};
    }
    std::string s(data_ + pos_, static_cast<std::size_t>(len));
    pos_ += static_cast<std::size_t>(len);
    return s;
  }

  /** @brief bc_instruction を読み込み */
  bc::instruction read_instruction() {
    bc::instruction inst;
    inst.op = static_cast<bc::opcode>(read_u8());
    inst.operand = read_u32_le();
    inst.operand2 = read_u32_le();
    inst.operand3 = read_u32_le();
    return inst;
  }

  /** @brief string_filter_entry を読み込み（文字列引数はリテラルインデックスから復元） */
  bc::string_filter_entry read_string_filter_entry(std::vector<std::string> const& literals) {
    bc::string_filter_entry e;
    e.filter = read_u8();
    e.arg1 = read_i32_le();
    e.arg2 = read_i32_le();
    auto idx1 = read_u64_le();
    auto idx2 = read_u64_le();
    if (idx1 < literals.size()) e.str_arg1 = literals[idx1];
    if (idx2 < literals.size()) e.str_arg2 = literals[idx2];
    return e;
  }

  /** @brief int_filter_entry を読み込み */
  bc::int_filter_entry read_int_filter_entry() {
    return {read_u8(), read_i32_le()};
  }

  /** @brief float_filter_entry を読み込み */
  bc::float_filter_entry read_float_filter_entry() {
    return {read_u8(), read_i32_le()};
  }

  /**
   * @brief bc_var_ref を読み込み
   * @param literals リテラルテーブル（文字列引数の復元に使用）
   */
  bc::var_ref read_var_ref(std::vector<std::string> const& literals) {
    bc::var_ref ref;
    ref.key = read_string();
    ref.has_dot = read_u8() != 0;
    ref.is_loop_parent = read_u8() != 0;
    ref.compare_rhs_kind = read_u8();
    ref.compare_rhs_text = read_string();
    ref.compare_rhs_has_dot = read_u8() != 0;
    ref.filter_flags = read_u8();
    if (version_ >= 3) {
      ref.section_op_count = read_u8();
      for (std::uint8_t si = 0; si < ref.section_op_count && si < bc::var_ref::max_section_ops; ++si) {
        ref.section_ops[si].kind = static_cast<injamm::detail::section_filter_op_kind>(read_u8());
        ref.section_ops[si].arg  = static_cast<std::int32_t>(read_u32_le());
        if (version_ >= 4)
          ref.section_ops[si].arg2 = static_cast<std::int32_t>(read_u32_le());
        if (version_ >= 5) {
          auto idx = read_u64_le();
          if (idx < literals.size()) ref.section_ops[si].str_arg1 = literals[static_cast<std::size_t>(idx)];
        }
      }
    } else if (version_ >= 2) {
      auto sec_rev = read_u8() != 0;
      auto sec_take_raw = read_u32_le();
      if (sec_rev) {
        ref.section_ops[0] = {injamm::detail::section_filter_op_kind::reverse, 0};
        ref.section_op_count = 1;
      }
      if (sec_take_raw > 0) {
        auto idx = ref.section_op_count;
        if (idx < bc::var_ref::max_section_ops) {
          ref.section_ops[idx] = {injamm::detail::section_filter_op_kind::take, static_cast<std::int32_t>(sec_take_raw - 1u)};
          ref.section_op_count = idx + 1;
        }
      }
    }

    if (ref.section_op_count > bc::var_ref::max_section_ops) { fail(); return ref; }

    auto fc = read_u64_le();
    if (failed_ || fc > max_filters) { fail(); return ref; }
    ref.filters.reserve(static_cast<std::size_t>(fc));
    for (std::uint64_t i = 0; i < fc && !failed_; ++i)
      ref.filters.push_back(read_string_filter_entry(literals));

    auto ifc = read_u64_le();
    if (failed_ || ifc > max_filters) { fail(); return ref; }
    ref.int_filters.reserve(static_cast<std::size_t>(ifc));
    for (std::uint64_t i = 0; i < ifc && !failed_; ++i)
      ref.int_filters.push_back(read_int_filter_entry());

    auto ffc = read_u64_le();
    if (failed_ || ffc > max_filters) { fail(); return ref; }
    ref.float_filters.reserve(static_cast<std::size_t>(ffc));
    for (std::uint64_t i = 0; i < ffc && !failed_; ++i)
      ref.float_filters.push_back(read_float_filter_entry());

    if (version_ >= 6) {
      ref.root_fallback = read_u8();
    }

    return ref;
  }

  /**
   * @brief バイトコード本体を読み込み
   * @details 命令列・リテラルテーブル・変数参照テーブル・partial を再帰的に読み込む。
   */
  bc::bytecode read_bytecode_body(int depth = 0) {
    if (depth > max_partial_depth) { fail(); return {}; }
    bc::bytecode out;
    out.is_simple = read_u8() != 0;
    out.literal_total_size = read_u64_le();
    if (failed_) return out;

    auto ic = read_u64_le();
    if (failed_ || ic > max_instructions) { fail(); return out; }
    out.instructions.reserve(static_cast<std::size_t>(ic));
    for (std::uint64_t i = 0; i < ic && !failed_; ++i)
      out.instructions.push_back(read_instruction());
    if (failed_) return out;

    auto lc = read_u64_le();
    if (failed_ || lc > max_literals) { fail(); return out; }
    out.literals.reserve(static_cast<std::size_t>(lc));
    std::uint64_t literal_bytes = 0;
    for (std::uint64_t i = 0; i < lc && !failed_; ++i) {
      out.literals.push_back(read_string());
      literal_bytes += out.literals.back().size();
    }
    if (failed_) return out;
    // 宣言された literal_total_size と実データの合計が一致しない .bc を拒否する
    if (out.literal_total_size != literal_bytes) { fail(); return out; }

    auto vc = read_u64_le();
    if (failed_ || vc > max_var_refs) { fail(); return out; }
    out.var_refs.reserve(static_cast<std::size_t>(vc));
    for (std::uint64_t i = 0; i < vc && !failed_; ++i)
      out.var_refs.push_back(read_var_ref(out.literals));
    if (failed_) return out;

    auto pc = read_u64_le();
    if (failed_ || pc > max_partials) { fail(); return out; }
    out.partial_entries.reserve(static_cast<std::size_t>(pc));
    for (std::uint64_t i = 0; i < pc && !failed_; ++i) {
      auto name = read_string();
      auto local = read_u8() != 0;
      if (failed_) break;
      auto partial_bc = new bc::bytecode(read_bytecode_body(depth + 1));
      out.partial_entries.push_back({std::move(name), local, partial_bc});
    }

    return out;
  }

  /** @brief opcode と operand の境界を検証する（library の bytecode_io と同基準） */
  static bool validate_compare_ref(bc::var_ref const& ref) {
    if (ref.key.empty() || ref.section_op_count != 0 ||
        ref.filter_flags != 0 || !ref.filters.empty() || !ref.float_filters.empty()) {
      return false;
    }
    auto const kind = static_cast<injamm::detail::compare_operand_kind>(ref.compare_rhs_kind);
    auto const eq_kind = static_cast<std::uint8_t>(injamm::detail::int_filter::eq);
    switch (kind) {
      case injamm::detail::compare_operand_kind::none:
      case injamm::detail::compare_operand_kind::int_literal:
        return ref.compare_rhs_text.empty() && !ref.compare_rhs_has_dot &&
               ref.int_filters.size() == 1 && ref.int_filters[0].filter == eq_kind;
      case injamm::detail::compare_operand_kind::string_literal:
        return ref.int_filters.empty() && !ref.compare_rhs_has_dot;
      case injamm::detail::compare_operand_kind::variable:
        return !ref.compare_rhs_text.empty() && ref.int_filters.empty();
      default:
        return false;
    }
  }

  bool validate_bytecode(bc::bytecode const& bc) {
    auto const n_lit = bc.literals.size();
    auto const n_var = bc.var_refs.size();
    auto const n_ins = bc.instructions.size();
    auto const n_par = bc.partial_entries.size();
    for (auto const& inst : bc.instructions) {
      bool bad = false;
      if (static_cast<unsigned>(inst.op) > static_cast<unsigned>(bc::opcode::halt)) { fail(); return false; }
      switch (inst.op) {
      case bc::opcode::emit_literal:
        bad = inst.operand >= n_lit;
        break;
      case bc::opcode::emit_litvar:
      case bc::opcode::emit_litvar_raw:
        bad = inst.operand >= n_lit || inst.operand2 >= n_var;
        break;
      case bc::opcode::emit_var:
      case bc::opcode::emit_var_raw:
      case bc::opcode::emit_var_size:
      case bc::opcode::emit_at_root_field:
      case bc::opcode::emit_at_root_field_raw:
        bad = inst.operand >= n_var;
        break;
      case bc::opcode::emit_section:
      case bc::opcode::emit_inverted:
        if (inst.operand2 >= n_var) bad = true;
        else if (inst.operand == 0 || inst.operand > n_ins) bad = true;
        else if (inst.operand3 != 0 && inst.operand3 > n_ins) bad = true;
        break;
      case bc::opcode::emit_if:
      case bc::opcode::emit_if_or:
      case bc::opcode::emit_if_and:
      case bc::opcode::emit_if_not:
      case bc::opcode::emit_if_filtered:
        if (inst.operand2 >= n_var) bad = true;
        else if (inst.operand == 0 || inst.operand > n_ins) bad = true;
        else if ((inst.op == bc::opcode::emit_if_or || inst.op == bc::opcode::emit_if_and) && inst.operand3 >= n_var) bad = true;
        break;
      case bc::opcode::emit_if_eq:
      case bc::opcode::emit_if_ne:
      case bc::opcode::emit_if_gt:
      case bc::opcode::emit_if_gte:
      case bc::opcode::emit_if_lt:
      case bc::opcode::emit_if_lte:
        if (inst.operand2 >= n_var) bad = true;
        else if (inst.operand == 0 || inst.operand > n_ins) bad = true;
        else if (!validate_compare_ref(bc.var_refs[inst.operand2])) bad = true;
        break;
      case bc::opcode::resolve_filtered:
        bad = inst.operand2 >= n_var;
        break;
      case bc::opcode::call_partial:
        bad = inst.operand >= n_par;
        break;
      case bc::opcode::emit_else:
      case bc::opcode::emit_endif:
      case bc::opcode::emit_end:
        bad = inst.operand != 0 && inst.operand > n_ins;
        break;
      case bc::opcode::emit_at_section:
      case bc::opcode::emit_at_inverted:
        bad = inst.operand == 0 || inst.operand > n_ins || inst.operand2 > 2;
        break;
      case bc::opcode::filter_string:
      case bc::opcode::filter_int:
      case bc::opcode::filter_float:
        bad = inst.operand2 > 64;
        break;
      default:
        break;
      }
      if (bad) { fail(); return false; }
    }
    for (auto const& pe : bc.partial_entries) {
      if (pe.bc && !validate_bytecode(*pe.bc)) return false;
    }
    enum class structural_kind { section_like, condition };
    struct structural_block {
      structural_kind kind;
      bool saw_else = false;
    };
    std::vector<structural_block> blocks;
    for (auto const& inst : bc.instructions) {
      switch (inst.op) {
        case bc::opcode::emit_section:
        case bc::opcode::emit_inverted:
        case bc::opcode::emit_at_section:
        case bc::opcode::emit_at_inverted:
          blocks.push_back({structural_kind::section_like, false});
          break;
        case bc::opcode::emit_if:
        case bc::opcode::emit_if_not:
        case bc::opcode::emit_if_or:
        case bc::opcode::emit_if_and:
        case bc::opcode::emit_if_eq:
        case bc::opcode::emit_if_ne:
        case bc::opcode::emit_if_gt:
        case bc::opcode::emit_if_gte:
        case bc::opcode::emit_if_lt:
        case bc::opcode::emit_if_lte:
        case bc::opcode::emit_if_filtered:
          blocks.push_back({structural_kind::condition, false});
          break;
        case bc::opcode::emit_else:
          if (blocks.empty() || blocks.back().saw_else) {
            fail();
            return false;
          }
          blocks.back().saw_else = true;
          break;
        case bc::opcode::emit_end:
          if (blocks.empty() || blocks.back().kind != structural_kind::section_like) {
            fail();
            return false;
          }
          blocks.pop_back();
          break;
        case bc::opcode::emit_endif:
          if (blocks.empty() || blocks.back().kind != structural_kind::condition) {
            fail();
            return false;
          }
          blocks.pop_back();
          break;
        default:
          break;
      }
    }
    if (!blocks.empty()) {
      fail();
      return false;
    }
    return true;
  }

  /**
   * @brief バイトコードを読み込み
   * @details マジック "IJBC" + バージョン 1/2/3 のヘッダを検証し、
   *          バイトコード本体を読み込む。バージョン 1 はセクションフィルタを
   *          含まない。バージョン 3 からパイプラインモデルを採用。
   * @return 正常に読み込まれた場合の bytecode、解析失敗時は nullopt
   */
  std::optional<bc::bytecode> read_bytecode() {
    char magic[4]{};
    if (pos_ + 4 > size_) return std::nullopt;
    std::memcpy(magic, data_ + pos_, 4);
    pos_ += 4;
    if (magic[0] != 'I' || magic[1] != 'J' || magic[2] != 'B' || magic[3] != 'C')
      return std::nullopt;

    auto version = read_u32_le();
    if (version == 1) {
      version_ = 1;
    } else if (version == 2) {
      version_ = 2;
    } else if (version == 3) {
      version_ = 3;
    } else if (version == 4) {
      version_ = 4;
    } else if (version == 5) {
      version_ = 5;
    } else if (version == 6) {
      version_ = 6;
    } else {
      return std::nullopt;
    }

    auto bc = read_bytecode_body();
    if (failed_ || !validate_bytecode(bc)) return std::nullopt;
    return bc;
  }
};

// ============================================================
// C++ コードジェネレーター
// ============================================================

/**
 * @brief バイトコードから C++ レンダリング関数を生成するクラス
 * @details バイトコードの命令列を走査し、各オペコードに対応する C++ コードを
 *          生成する。生成される関数は glaze に非依存で、直接フィールドアクセス
 *          による高速なレンダリングが可能。
 */
class code_generator {
  std::string type_name_;      /**< データ型名 */
  std::string namespace_;      /**< 生成コードの名前空間 */
  std::string func_prefix_;    /**< 関数名プレフィックス（空なら "render"） */
  bool no_simd_ = false;       /**< SIMD命令を生成しない */
  int indent_ = 0;             /**現在のインデントレベル */
  int scope_depth_ = 0;        /**< 現在の値スコープ深度（VM ループ深度とは異なる） */
  bool in_partial_ = false;    /**< partial 本体の生成中（値スコープ式が value、ルート式が root） */
  /**< インデックス形式（_iN/_sizeN）が必要な emit_section 命令 */
  std::unordered_set<const bc::instruction*> index_loops_;
  /**< collect_two_stage_keys のメタデータ走査で発見したインデックス形式ループ */
  std::unordered_set<const bc::instruction*> metadata_index_loops_;
  enum class block_syntax_kind { section, inverted, condition, at_condition, top_level };
  enum class block_phase { main, else_body };
  enum class runtime_category { container, scalar_optional, inverted, if_branch, top_level };

  struct control_ownership {
    block_syntax_kind syntax = block_syntax_kind::top_level;
    runtime_category runtime = runtime_category::top_level;
    int id = 0;
    int parent_id = 0;
    int loop_depth = 0;
    std::string local_channel;
    std::string parent_channel;
    std::string root_expression;
    std::string loop_context;
  };

  std::unordered_map<const bc::instruction*, control_ownership> control_ownership_;
  int next_control_id_ = 0;

  /** @brief emit_end/emit_endif で閉じる構文ブロック */
  struct open_block {
    block_syntax_kind syntax = block_syntax_kind::condition;
    block_phase phase = block_phase::main;
    runtime_category runtime = runtime_category::if_branch;
    int control_id = 0;
    std::string local_control;
    std::string parent_control;
    std::string root_expression;
    std::string loop_context;
    int loop_depth = 0;
    std::string access;   /**< scalar_section/inverted: セクション対象のアクセス式 */
    std::string container_access; /**< フィルタ済みコンテナの値/ルート二段解決式 */
    int         idx = 0;  /**< scalar_section: 本体の _itemN の N（スコープ解決に使うループ深度） */
    int         seq = 0;  /**< scalar_section/inverted: 本体ラムダ名の連番（兄弟セクションで衝突させない） */
    int         else_seq = 0;  /**< {{else}} 本体ラムダ名の連番（else なしは 0） */
    std::string two_stage_key; /**< 二段解決の対象キー（空なら二段解決なし。ディスパッチのガードに使う） */
    bool        root_param = false;    /**< 本体ラムダがルート引数（_rtN）を持つ */
    bool        else_root_param = false; /**< else ラムダがルート引数（_rtN）を持つ */
    std::string call_root;  /**< ラムダに入る前のルート式（呼び出し引数と復元に使う） */
    std::string call_loop;  /**< ラムダに入る前の VM ループ情報 */
    bool        indexed = false; /**< コンテナ分岐で index/size を生成する必要がある */
    const bc::instruction* inst = nullptr; /**< 開始命令（マーキング参照用） */
    bool filtered = false;
  };
  int lambda_seq_ = 0; /**< 本体ラムダ名の連番 */
  std::vector<open_block> open_blocks_;
  std::set<std::string> two_stage_keys_; /**< 二段解決ヘルパを生成するキー一覧（昇順） */
  std::set<const bc::instruction*> root_param_main_;   /**< 本体ラムダにルート引数が必要なセクション */
  std::set<const bc::instruction*> root_param_else_;   /**< else ラムダにルート引数が必要なセクション */
  std::set<const bc::instruction*> root_param_inv_;    /**< 本体ラムダにルート引数が必要な逆セクション */
  std::string cur_root_; /**< 現在のラムダ階層でのルート式（空なら root_expr()） */

  /** @brief 現在の生成位置でルートを参照する式を返す
   *  @details ルート引数付きラムダ内ではその引数（_rtN）、それ以外は data / root。 */
  std::string root_for_emit() const { return cur_root_.empty() ? root_expr() : cur_root_; }
  /** @brief 現在の VM ループ情報を参照する生成式 */
  std::string loop_context_expr() const {
    if (scope_depth_ > 0) return "_loop" + std::to_string(scope_depth_);
    return in_partial_ ? "_loop" : "codegen_loop_context{}";
  }

  std::string active_control_channel() const {
    for (auto it = open_blocks_.rbegin(); it != open_blocks_.rend(); ++it) {
      if (!it->local_control.empty()) return it->local_control;
    }
    return "_root_control";
  }

  bool in_control_lambda() const {
    for (auto const& block : open_blocks_) {
      if (block.syntax == block_syntax_kind::section ||
          block.syntax == block_syntax_kind::inverted) {
        return true;
      }
    }
    return false;
  }

  void emit_control_stop(std::string const& channel) {
    emit("if (" + channel + ".break_requested || " + channel + ".continue_requested) {");
    ++indent_;
    emit(in_control_lambda() ? "return;" : "return {};");
    --indent_;
    emit("}");
  }

  std::string unsupported_feature_;  /**< generate() が対応不能な命令を検出した場合の説明 */
  std::ostringstream out_;     /**< 出力ストリーム */

  // フィルタの文字列引数は VM と同様 var_ref.filters に格納され、filter_string
  // オペコードの operand3 には入らない。resolve_filtered で現在の var_ref を記録し、
  // 後続の filter_string が対応エントリから文字列引数を読めるよう位置を追跡する。
  bc::var_ref const* cur_filter_ref_ = nullptr; /**< フィルタチェーン中の現在の var_ref */
  std::size_t filter_str_pos_ = 0;              /**< filter_string の処理位置 */

  /**
   * @brief インデント付きで 1 行出力
   * @param line 出力する行
   */
  void emit(std::string_view line) {
    for (int i = 0; i < indent_; ++i) out_ << "  ";
    out_ << line << '\n';
  }

  /** @brief インデントなしで 1 行出力 */
  void emit_raw(std::string_view line) {
    out_ << line << '\n';
  }

  /**
   * @brief 文字列を C++ の文字列リテラルに変換
   * @details バックスラッシュ、ダブルクォート、改行、タブをエスケープする
   */
  std::string cpp_string(std::string_view s) const {
    std::string result = "\"";
    for (char c : s) {
      switch (c) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\t': result += "\\t"; break;
        default: result += c; break;
      }
    }
    result += '"';
    return result;
  }

  /**
   * @brief 現在の生成コンテキストでの「ルート」式を返す
   * @details メイン関数では data、partial 本体では render_partial_impl の root 引数。
   */
  std::string root_expr() const { return in_partial_ ? "root" : "data"; }

  /**
   * @brief 現在の生成コンテキストでの「現在値」式を返す
   * @details ループ内は要素ラムダ引数 _itemN、メイン関数トップレベルでは data、
   *          partial 本体トップレベルでは render_partial_impl の value 引数。
   */
  std::string value_expr() const {
    if (scope_depth_ > 0) return "_item" + std::to_string(scope_depth_);
    return in_partial_ ? "value" : "data";
  }

  /**
   * @brief 参照が現在値を先に探索する必要があるか判定
   * @details partial は呼び出し時の値型がルート型と異なるため常に二段解決する。
   *          通常ループではコンパイル時にルートのみと実証済みの参照だけを直接解決する。
   */
  bool is_value_relative(bc::var_ref const& ref) const {
    if (ref.is_loop_parent ||
        injamm::detail::classify_special_var(ref.key) != injamm::detail::special_var_kind::none) {
      return false;
    }
    if (in_partial_) {
      return true;
    }
    return scope_depth_ > 0 && ref.root_fallback != injamm::detail::root_fb_proven;
  }

  /**
   * @brief 変数参照を C++ のフィールドアクセス式に変換
   * @details ループ内では _item{depth}.field、ルートでは data.field を返す。
   *          ネストパス（ドット区切り）は data.path.to.field に展開する。
   *          partial 本体では値スコープが value、ルートが root になる。
   */
  std::string resolve_access(bc::var_ref const& ref) const {
    if (ref.is_loop_parent) {
      if (ref.has_dot) {
        auto pos = ref.key.find("loop.parent.");
        auto path = (pos != std::string::npos) ? ref.key.substr(pos + 12) : ref.key;
        return root_expr() + "." + path;
      }
      return root_expr();
    }

    /** ルート型フォールバック参照: ルートデータのフィールドを直接アクセスする */
    if (ref.root_fallback == injamm::detail::root_fb_proven && !in_partial_) {
      return root_expr() + "." + ref.key;
    }

    if (is_value_relative(ref)) {
      return value_expr() + "." + ref.key;
    }

    if (ref.has_dot) {
      return root_expr() + "." + ref.key;
    }

    if (in_partial_) {
      return "value." + ref.key;
    }

    return root_expr() + "." + ref.key;
  }

  /**
   * @brief 二段解決ヘルパの呼び出し式を返す
   * @details 値側にキーが無い場合にルートへフォールバックするヘルパ関数テンプレート
   *          （emit_header でキーごとに生成）を呼び出す式を組み立てる。
   *          ジェネリックラムダ内に `if constexpr (requires { _item1.key; })`
   *          を直接出力すると、Clang / MSVC がラムダ式の実体化時に破棄側
   *          （data.key）を検証してしまい、ルート型にキーが無いとコンパイル
   *          エラーになる。関数テンプレート内の if constexpr は破棄側が
   *          遅延実体化されるため、ヘルパ経由なら全コンパイラで動作する。
   */
  std::string two_stage_access(std::string const& key) const {
    return cgns_name() + "::cg_get_" + hex_encode(key) + "(" + value_expr() + ", " + root_expr() + ")";
  }

  /** @brief 二段解決ヘルパを格納する名前空間名（関数名プレフィックス由来でヘッダごとに一意） */
  std::string cgns_name() const { return "cgns_" + (func_prefix_.empty() ? std::string("render") : func_prefix_); }

  /** @brief 任意のバイト列を大文字16進表現へ一意に変換 */
  static std::string hex_encode(std::string const& value) {
    std::string s;
    s.reserve(value.size() * 2);
    constexpr std::string_view digits = "0123456789ABCDEF";
    for (auto const c : value) {
      auto const uc = static_cast<unsigned char>(c);
      s += digits[uc >> 4];
      s += digits[uc & 0x0F];
    }
    return s;
  }

  /** @brief 生成ヘッダ固有のプリプロセッサガード名 */
  std::string header_guard_name() const {
    auto const prefix = func_prefix_.empty() ? std::string{"render"} : func_prefix_;
    return "INJAMM_CODEGEN_NS_" + hex_encode(namespace_) + "_FN_" + hex_encode(prefix) + "_HPP";
  }

  /** @brief 同一生成名前空間で public partial fallback を一度だけ定義するガード名 */
  std::string partial_fallback_guard_name() const {
    return "INJAMM_CODEGEN_PARTIAL_FALLBACK_NS_" + hex_encode(namespace_) + "_HPP";
  }

  /** @brief partial 内部ディスパッチの生成ヘッダ固有名 */
  std::string partial_impl_name() const {
    auto const prefix = func_prefix_.empty() ? std::string{"render"} : func_prefix_;
    return prefix + "_partial_impl";
  }

  /** @brief public partial overload を識別する制約用タグ名 */
  std::string partial_tag_name() const {
    auto const prefix = func_prefix_.empty() ? std::string{"render"} : func_prefix_;
    return prefix + "_partial_tag";
  }

  /** @brief partial を prefix 単位で明示選択できる public API 名 */
  std::string prefixed_partial_api_name() const {
    auto const prefix = func_prefix_.empty() ? std::string{"render"} : func_prefix_;
    return prefix == "render" ? std::string{"render_partial"} : prefix + "_partial";
  }

  /** @brief 現在値 → ルートの二段解決を含むアクセス式を返す */
  std::string resolve_value_access(bc::var_ref const& ref) const {
    return is_value_relative(ref) ? two_stage_access(ref.key) : resolve_access(ref);
  }

  /**
   * @brief 値参照を使った文を出力する
   * @details 値スコープ相対参照（ループ内の _itemN.key / partial 本体トップレベルの
   *          value.key）では、キーが値側に無ければルートへフォールバックする二段解決
   *          ヘルパ（two_stage_access）経由で出力する
   *          （VM handle_call_partial の value → root と同一）。
   * @param prefix 文の前半（アクセス式の直前まで）
   * @param suffix 文の後半（アクセス式の直後から）
   * @param ref 対象の変数参照
   */
  void emit_value_ref(std::string const& prefix, std::string const& suffix, bc::var_ref const& ref) {
    auto acc = resolve_value_access(ref);
    emit(prefix + acc + suffix);
  }

  /**
   * @brief if 条件式を C++ の真偽式に変換
   * @details loop.is_last/loop.is_first は現在のループのインデックス比較に変換する。
   *          （{{#if loop.is_last}} 等。emit_at_section 相当の最適化を if 構文にも適用）
   */
  std::string resolve_if_access(bc::var_ref const& ref) {
    auto const loop = loop_context_expr();
    switch (injamm::detail::classify_special_var(ref.key)) {
      case injamm::detail::special_var_kind::loop_index:
        return "(" + loop + ".active && " + loop + ".index != 0)";
      case injamm::detail::special_var_kind::loop_index1:
        return loop + ".active";
      case injamm::detail::special_var_kind::loop_size:
        return "(" + loop + ".active && " + loop + ".size != 0)";
      case injamm::detail::special_var_kind::loop_is_first:
        return "(" + loop + ".active && " + loop + ".index == 0)";
      case injamm::detail::special_var_kind::loop_is_last:
        return "(" + loop + ".active && " + loop + ".index + 1 == " + loop + ".size)";
      case injamm::detail::special_var_kind::loop_is_even:
        return "(" + loop + ".active && " + loop + ".index % 2 == 0)";
      case injamm::detail::special_var_kind::loop_is_odd:
        return "(" + loop + ".active && " + loop + ".index % 2 == 1)";
      case injamm::detail::special_var_kind::loop_unknown:
        return "false";
      case injamm::detail::special_var_kind::none:
        break;
      default:
        return "false";
    }
    return resolve_value_access(ref);
  }

  /** @brief 比較命令の LHS が現在の VM ループ情報を使うか */
  static bool comparison_uses_loop_context(bc::var_ref const& ref) {
    auto const special = injamm::detail::classify_special_var(ref.key);
    return special == injamm::detail::special_var_kind::this_ ||
           special == injamm::detail::special_var_kind::loop_index ||
           special == injamm::detail::special_var_kind::loop_index1 ||
           special == injamm::detail::special_var_kind::loop_size;
  }

  /** @brief RHS 特殊変数がループコンテキストを必須とするか */
  static bool loop_special_requires_active(injamm::detail::special_var_kind special) {
    return special == injamm::detail::special_var_kind::loop_index ||
           special == injamm::detail::special_var_kind::loop_index1 ||
           special == injamm::detail::special_var_kind::loop_size;
  }

  /** @brief 比較命令の左辺を C++ 式へ変換する */
  std::string resolve_compare_lhs(bc::var_ref const& ref) const {
    auto const special = injamm::detail::classify_special_var(ref.key);
    auto const loop = loop_context_expr();
    switch (special) {
      case injamm::detail::special_var_kind::none:
        return resolve_value_access(ref);
      case injamm::detail::special_var_kind::this_:
        return value_expr();
      case injamm::detail::special_var_kind::loop_index:
        return loop + ".index";
      case injamm::detail::special_var_kind::loop_index1:
        return loop + ".index + 1";
      case injamm::detail::special_var_kind::loop_size:
        return loop + ".size";
      default:
        return "std::optional<int>{}";
    }
  }

  /** @brief 比較 RHS の変数参照を通常の value → root 解決規則へ写す */
  static bc::var_ref make_compare_rhs_ref(bc::var_ref const& ref) {
    bc::var_ref rhs_ref;
    rhs_ref.key = ref.compare_rhs_text;
    rhs_ref.has_dot = ref.compare_rhs_has_dot;
    rhs_ref.is_loop_parent = std::string_view{ref.compare_rhs_text}.starts_with("loop.parent.");
    rhs_ref.root_fallback = injamm::detail::root_fb_unproven;
    return rhs_ref;
  }

  /** @brief 比較 RHS が loop.index 系の実行時ループ情報を使うか */
  static bool comparison_rhs_uses_loop_context(bc::var_ref const& ref) {
    if (static_cast<injamm::detail::compare_operand_kind>(ref.compare_rhs_kind) !=
        injamm::detail::compare_operand_kind::variable) {
      return false;
    }
    auto const rhs_ref = make_compare_rhs_ref(ref);
    if (rhs_ref.is_loop_parent) {
      return false;
    }
    return loop_special_requires_active(injamm::detail::classify_special_var(rhs_ref.key));
  }

  /** @brief compare_rhs_kind に応じた比較 RHS の C++ 式を返す */
  std::string get_compare_rhs_expression(bc::var_ref const& ref) const {
    auto const kind = static_cast<injamm::detail::compare_operand_kind>(ref.compare_rhs_kind);
    switch (kind) {
      case injamm::detail::compare_operand_kind::int_literal: {
        return std::to_string(ref.int_filters[0].arg);
      }
      case injamm::detail::compare_operand_kind::string_literal: {
        return "std::string_view{" + cpp_string(ref.compare_rhs_text) + ", " +
               std::to_string(ref.compare_rhs_text.size()) + "u}";
      }
      case injamm::detail::compare_operand_kind::variable: {
        auto const rhs_ref = make_compare_rhs_ref(ref);
        if (rhs_ref.is_loop_parent) {
          return resolve_value_access(rhs_ref);
        }
        auto const special = injamm::detail::classify_special_var(rhs_ref.key);
        auto const loop = loop_context_expr();
        switch (special) {
          case injamm::detail::special_var_kind::none:
            return resolve_value_access(rhs_ref);
          case injamm::detail::special_var_kind::this_:
            return value_expr();
          case injamm::detail::special_var_kind::loop_index:
            return loop + ".index";
          case injamm::detail::special_var_kind::loop_index1:
            return loop + ".index + 1";
          case injamm::detail::special_var_kind::loop_size:
            return loop + ".size";
          default:
            return "std::optional<int>{}";
        }
      }
      case injamm::detail::compare_operand_kind::none: {
        return std::to_string(ref.int_filters[0].arg);
      }
      default: {
        return "std::optional<int>{}";
      }
    }
  }

  /** @brief 比較オペコードを生成ヘッダ側の enum 式へ変換する */
  static std::string compare_opcode_expression(bc::opcode op) {
    switch (op) {
      case bc::opcode::emit_if_eq: {
        return "bc_opcode::emit_if_eq";
      }
      case bc::opcode::emit_if_ne: {
        return "bc_opcode::emit_if_ne";
      }
      case bc::opcode::emit_if_gt: {
        return "bc_opcode::emit_if_gt";
      }
      case bc::opcode::emit_if_gte: {
        return "bc_opcode::emit_if_gte";
      }
      case bc::opcode::emit_if_lt: {
        return "bc_opcode::emit_if_lt";
      }
      case bc::opcode::emit_if_lte: {
        return "bc_opcode::emit_if_lte";
      }
      default: {
        return "bc_opcode::emit_if_eq";
      }
    }
  }

  /** @brief 比較 RHS 種別を生成ヘッダ側の enum 式へ変換する */
  static std::string compare_rhs_kind_expression(bc::var_ref const& ref) {
    auto const kind = static_cast<injamm::detail::compare_operand_kind>(ref.compare_rhs_kind);
    switch (kind) {
      case injamm::detail::compare_operand_kind::int_literal: {
        return "compare_operand_kind::int_literal";
      }
      case injamm::detail::compare_operand_kind::string_literal: {
        return "compare_operand_kind::string_literal";
      }
      case injamm::detail::compare_operand_kind::variable: {
        return "compare_operand_kind::variable";
      }
      case injamm::detail::compare_operand_kind::none: {
        return "compare_operand_kind::none";
      }
      default: {
        return "compare_operand_kind::none";
      }
    }
  }

  /** @brief ヘッダファイルの先頭（インクルードガード + include）を生成 */
  void emit_header() {
    auto const guard = header_guard_name();
    emit_raw("#pragma once");
    emit_raw("#ifndef " + guard);
    emit_raw("#define " + guard);
    emit_raw("/**");
    emit_raw(" * @file render.hpp");
    emit_raw(" * @brief injamm_codegen によって自動生成されたレンダリング関数");
    emit_raw(" */");
    emit_raw("");
    emit_raw("#include <expected>");
    emit_raw("#include <string>");
    emit_raw("");
    emit_raw("#include <injamm/types.hpp>");
    emit_raw("#include <glaze/glaze.hpp>");
    if (no_simd_) {
      emit_raw("#define INJAMM_CODEGEN_DISABLE_SIMD 1");
    } else {
      emit_raw("#include <injamm/escape.hpp>");
    }
    emit_raw("#include \"codegen_helpers.hpp\"");
    emit_raw("");
    emit_raw("namespace " + namespace_ + " {");
    emit_helpers();
    emit_raw("");
  }

  /**
   * @brief 二段解決ヘルパ（キーごとの関数テンプレート）を生成
   * @details 値側にキーが無い場合にルートへフォールバックする。関数テンプレート内の
   *          if constexpr は破棄側が遅延実体化されるため、ジェネリックラムダ内に
   *          requires ガードを直接出力した場合と異なり Clang / MSVC でも動作する。
   *          関数名プレフィックス由来の名前空間に格納し、同一名前空間に複数の
   *          生成ヘッダをインクルードしても衝突しない。
   */
  void emit_helpers() {
    emit_raw("");
    emit_raw("namespace " + cgns_name() + " {");
    emit_raw("struct control_slot {");
    emit_raw("  bool break_requested = false;");
    emit_raw("  bool continue_requested = false;");
    emit_raw("};");
    emit_raw("} // namespace " + cgns_name());
    if (two_stage_keys_.empty()) return;
    emit_raw("");
    emit_raw("namespace " + cgns_name() + " {");
    for (auto const& key : two_stage_keys_) {
      auto const fname = "cg_get_" + hex_encode(key);
      emit_raw("/**");
      emit_raw(" * @brief 値側に @p key が無ければルート側へフォールバックする二段解決ヘルパ");
      emit_raw(" * @details VM (bytecode_exec.hpp) の value → root 二段走査と同一の解決順。");
      emit_raw(" */");
      emit_raw("template <class V, class R>");
      emit_raw("inline decltype(auto) " + fname + "(V const& v, R const& r) {");
      emit_raw("  if constexpr (requires { v." + key + "; }) {");
      emit_raw("    return (v." + key + ");");
      emit_raw("  } else {");
      emit_raw("    return (r." + key + ");");
      emit_raw("  }");
      emit_raw("}");
    }
    emit_raw("} // namespace " + cgns_name());
  }

  /** @brief ヘルパ関数の生成（共通ヘッダを使用するため不要） */
  void emit_escape_func() {}
  void emit_number_conv() {}
  void emit_filter_helpers() {}

  /**
   * @brief 出力先バッファ版レンダリング関数の先頭を生成
   * @details render(data, out) -> expected<void> のシグニチャと本体冒頭。
   *          既存の std::string を出力先として受け取りバッファを再利用する。
   *          任意の sink（callback_sink 等）にも対応する（テンプレート sink 方式）。
   * @param reserve_size 文字列バッファの事前確保サイズ（バイト）
   * @param declare_filtered フィルタバッファ _filtered を先頭で宣言するか
   */
  void emit_render_into_start(std::size_t reserve_size = 256, bool declare_filtered = false) {
    auto func_name = func_prefix_.empty() ? "render" : func_prefix_;
    emit_raw("");
    emit_raw("/**");
    emit_raw(" * @brief テンプレート文字列から生成されたレンダリング関数（バッファ再利用版）");
    emit_raw(" *");
    emit_raw(" * @details injamm_codegen によって自動生成された関数。");
    emit_raw(" *          出力先を引数で受け取る。std::string ならバッファを再利用し、");
    emit_raw(" *          injamm::detail::output_sink を満たす型（callback_sink 等）なら");
    emit_raw(" *          ストリーミング出力する。");
    emit_raw(" *");
    emit_raw(" * @tparam T    データ型（フィールドへのアクセスが必要）");
    emit_raw(" * @tparam Sink 出力先型（std::string または output_sink を満たす型）");
    emit_raw(" * @param data レンダリング対象のデータ");
    emit_raw(" * @param out  出力先（std::string なら内容はクリアされる）");
    emit_raw(" * @return 正常時: void。エラー時: error_ctx");
    emit_raw(" */");
    emit_raw("template <typename T, typename Sink = std::string>");
    emit_raw("  requires injamm::detail::output_sink<Sink>");
    emit_raw("[[nodiscard]] std::expected<void, injamm::error_ctx>");
    emit_raw(func_name + "(const T& data, Sink& out) {");
    ++indent_;
    emit("if constexpr (std::is_same_v<Sink, std::string>) {");
    ++indent_;
    emit("out.clear();");
    if (reserve_size > 0) {
      emit("out.reserve(" + std::to_string(reserve_size) + ");");
    }
    --indent_;
    emit("}");
    if (declare_filtered) {
      // フィルタバッファは関数先頭で宣言し assign で再利用する
      // （ループ内フィルタで毎回アロケーションしないための工夫）
      emit("std::string _filtered;");
      emit("_filtered.reserve(64);");
    }
    emit(cgns_name() + "::control_slot _root_control;");
    emit_raw("");
  }

  /** @brief 出力先バッファ版レンダリング関数の末尾を生成 */
  void emit_render_into_end() {
    emit_raw("");
    emit("return {};");
    --indent_;
    emit_raw("}");
    emit_raw("");
  }

  /**
   * @brief アロケーション版レンダリング関数（ラッパー）を生成
   * @details render(data) -> expected<std::string>。
   *          内部で render(data, out) を呼び出し結果を返す。
   */
  void emit_render_wrapper_start() {
    auto func_name = func_prefix_.empty() ? "render" : func_prefix_;
    emit_raw("/**");
    emit_raw(" * @brief テンプレート文字列から生成されたレンダリング関数");
    emit_raw(" *");
    emit_raw(" * @details injamm_codegen によって自動生成された関数。");
    emit_raw(" *          バッファ再利用版 (render(data, out)) のラッパー。");
    emit_raw(" *");
    emit_raw(" * @tparam T データ型（フィールドへのアクセスが必要）");
    emit_raw(" * @param data レンダリング対象のデータ");
    emit_raw(" * @return 正常時: レンダリング結果文字列。エラー時: error_ctx");
    emit_raw(" *");
    emit_raw(" * @code");
    emit_raw(" *   // 使い方例:");
    emit_raw(" *   #include \"render.hpp\"");
    emit_raw(" *");
    emit_raw(" *   struct UserData { std::string name; int age; };");
    emit_raw(" *   UserData user{\"Alice\", 30};");
    emit_raw(" *   auto result = generated::render(user);");
    emit_raw(" *   if (result) std::cout << *result << std::endl;");
    emit_raw(" * @endcode");
    emit_raw(" */");
    emit_raw("template <typename T>");
    emit_raw("[[nodiscard]] std::expected<std::string, injamm::error_ctx>");
    emit_raw(func_name + "(const T& data) {");
    ++indent_;
    emit("std::string out;");
    emit("auto result = " + func_name + "(data, out);");
    emit("if (!result) return std::unexpected(result.error());");
    emit("return out;");
    --indent_;
    emit_raw("}");
    emit_raw("");
  }

  /**
   * @brief 名前付き partial のディスパッチ関数 render_partial を生成
   * @details render_partial<"name">(data, out) の if constexpr ディスパッチ。
   *          non-local な partial エントリごとに分岐を生成する。
   *          partial がない場合は何も出力しない。
   *
   *          NTTP に injamm::fixed_string を使うことで、利用側は文字列リテラルで
   *          partial 名を指定できる。コンパイル時に if constexpr で解決されるため、
   *          該当ブランチ以外のコードは生成されない。
   */
  void emit_partial_dispatch(bc::bytecode const& bc) {
    /* レンダリング対象の partial でかつ空でないバイトコードを持つものの数を数える。
       local partial も対象に含める: 旧実装は call_partial の emit 時に local を
       見落としていたため、{{#partialdef x local}}...{{>x}} の本文が生成コードから
       無音で欠落していた（VM は描画するのに codegen は描画しない）。
       local は外部の render_partial<"x"> 呼び出しから隠す意図の名前だが、
       main 本体の call_partial は同名 dispatch を経由すれば良い。 */
    std::size_t partial_count = 0;
    for (auto const& pe : bc.partial_entries) {
      if (pe.bc && !pe.bc->instructions.empty())
        ++partial_count;
    }
    /* partial が1つもなければ dispatch 関数を出力しない */
    if (partial_count == 0) return;
    auto const impl_name = partial_impl_name();
    auto const tag_name = partial_tag_name();
    auto const prefixed_api_name = prefixed_partial_api_name();
    std::string known_constraint;
    for (auto const& pe : bc.partial_entries) {
      if (!pe.bc || pe.bc->instructions.empty()) continue;
      if (!known_constraint.empty()) {
        known_constraint += " || ";
      }
      known_constraint += "std::string_view(PartialName.data) == " + cpp_string(pe.name);
    }

    emit_raw("");
    emit_raw("/**");
    emit_raw(" * @brief 名前付き partial をレンダリングする内部実装");
    emit_raw(" *");
    emit_raw(" * @details 値 (value) とルート (root) を分けて受け取り、本体は");
    emit_raw(" *          「値 → ルート」の二段解決で生成される（VM handle_call_partial と同一）。");
    emit_raw(" *          ループ内から呼ばれたグローバル partial も現在の要素を優先して解決する。");
    emit_raw(" */");
    /* partial 間の相互参照・ネスト呼び出しのために前方宣言しておく */
    emit_raw("template <injamm::fixed_string PartialName, typename V, typename R, typename Sink>");
    emit_raw("  requires injamm::detail::output_sink<Sink>");
    emit_raw("[[nodiscard]] std::expected<void, injamm::error_ctx>");
    emit_raw(impl_name + "(const V& value, const R& root, const codegen_loop_context& _loop, " +
             cgns_name() + "::control_slot& _root_control, Sink& out);");
    emit_raw("");
    emit_raw("template <injamm::fixed_string PartialName, typename V, typename R, typename Sink>");
    emit_raw("  requires injamm::detail::output_sink<Sink>");
    emit_raw("[[nodiscard]] std::expected<void, injamm::error_ctx>");
    emit_raw(impl_name + "(const V& value, const R& root, const codegen_loop_context& _loop, " +
             cgns_name() + "::control_slot& _root_control, Sink& out) {");
    ++indent_;

    /* if constexpr チェーン: 各 partial 名を std::string_view で比較する。
       コンパイル時解決なので実行時オーバーヘッドは一切ない。
       local partial も含める（call_partial が参照するため。main 本体以外から
       render_partial<"name"> で直接呼ぶことは文書上非推奨という扱いで許容）。 */
    bool first = true;
    for (auto const& pe : bc.partial_entries) {
      if (!pe.bc || pe.bc->instructions.empty()) continue;

      auto name_lit = cpp_string(pe.name);
      if (first) {
        emit("if constexpr (std::string_view(PartialName.data) == " + name_lit + ") {");
        first = false;
      } else {
        emit("else if constexpr (std::string_view(PartialName.data) == " + name_lit + ") {");
      }
      ++indent_;

      /* scope_depth_ などの状態を退避: 各 partial は独立した関数内部と同じ扱い。
         値スコープは value、ルートは root 引数になる。 */
      auto saved_scope   = scope_depth_;
      auto saved_blocks  = std::move(open_blocks_);
      auto saved_partial = in_partial_;
      scope_depth_ = 0;
      open_blocks_.clear();
      in_partial_ = true;
      precompute_control_ownership(*pe.bc);

      /* _filtered を使う partial はブロック先頭で宣言する（ループ内フィルタの再アロケーション回避） */
      if (uses_filtered(*pe.bc)) {
        emit("std::string _filtered;");
        emit("_filtered.reserve(64);");
      }
      precompute_index_loops(*pe.bc);

      for (auto const& pi : pe.bc->instructions) {
        emit_instruction(pi, *pe.bc);
      }

      emit("return {};");

      scope_depth_  = saved_scope;
      open_blocks_ = std::move(saved_blocks);
      in_partial_  = saved_partial;

      --indent_;
      emit_raw("}");
    }

    /* どの partial 名にも一致しなかった場合はエラーを返す */
    emit("else {");
    ++indent_;
    emit("return std::unexpected(injamm::error_ctx{.ec = injamm::error_code::unknown_key, .custom_error_message = \"unknown partial: \" + std::string(PartialName.data)});");
    --indent_;
    emit_raw("}");

    --indent_;
    emit_raw("}");

    /* partial 名が一致する overload だけを有効化し、同一名前空間の複数ヘッダでも
       内部 dispatcher と public API が衝突しないようにする。 */
    emit_raw("");
    emit_raw("struct " + tag_name + " {};");
    emit_raw("");
    emit_raw("/**");
    emit_raw(" * @brief 名前付き partial を NTTP で指定してレンダリングする");
    emit_raw(" *");
    emit_raw(" * @details injamm_codegen によって自動生成された関数。");
    emit_raw(" *          PartialName に partial 名を NTTP 文字列として渡す。");
    emit_raw(" *          存在しない名前の場合は unknown_key エラーを返す。");
    emit_raw(" *");
    emit_raw(" * @tparam T          データ型");
    emit_raw(" * @tparam PartialName partial 名（fixed_string 互換 NTTP）");
    emit_raw(" * @tparam Sink       出力先型（std::string または output_sink を満たす型）");
    emit_raw(" * @param data レンダリング対象のデータ");
    emit_raw(" * @param out  出力先バッファ（内容は追記される）");
    emit_raw(" * @return 正常時: void。エラー時: error_ctx");
    emit_raw(" *");
    emit_raw(" * @code");
    emit_raw(" *   // 使い方例:");
    emit_raw(" *   std::string out;");
    emit_raw(" *   auto result = generated::render_partial<\"header\">(data, out);");
    emit_raw(" * @endcode");
    emit_raw(" */");
    emit_raw("template <injamm::fixed_string PartialName, typename T, typename Sink = std::string>");
    emit_raw("  requires injamm::detail::output_sink<Sink> && (" + known_constraint + ")");
    emit_raw("        && std::same_as<" + tag_name + ", " + tag_name + ">");
    emit_raw("[[nodiscard]] std::expected<void, injamm::error_ctx>");
    emit_raw("render_partial(const T& data, Sink& out) {");
    ++indent_;
    emit(cgns_name() + "::control_slot _root_control;");
    emit("return " + impl_name + "<PartialName>(data, data, codegen_loop_context{}, _root_control, out);");
    --indent_;
    emit_raw("}");
    emit_raw("");

    if (prefixed_api_name != "render_partial") {
      emit_raw("template <injamm::fixed_string PartialName, typename T, typename Sink = std::string>");
      emit_raw("  requires injamm::detail::output_sink<Sink>");
      emit_raw("[[nodiscard]] std::expected<void, injamm::error_ctx>");
      emit_raw(prefixed_api_name + "(const T& data, Sink& out) {");
      ++indent_;
      emit(cgns_name() + "::control_slot _root_control;");
      emit("return " + impl_name + "<PartialName>(data, data, codegen_loop_context{}, _root_control, out);");
      --indent_;
      emit_raw("}");
      emit_raw("");
    }

    /* 公開 API の未知 partial は従来どおり unknown_key を返す。
       各名前空間につき一度だけ定義し、別 prefix の生成ヘッダは上記の
       制約付き overload だけを追加する。 */
    auto const fallback_guard = partial_fallback_guard_name();
    emit_raw("#ifndef " + fallback_guard);
    emit_raw("#define " + fallback_guard);
    emit_raw("template <injamm::fixed_string PartialName, typename T, typename Sink = std::string>");
    emit_raw("  requires injamm::detail::output_sink<Sink>");
    emit_raw("[[nodiscard]] std::expected<void, injamm::error_ctx>");
    emit_raw("render_partial(const T&, Sink&) {");
    ++indent_;
    emit("return std::unexpected(injamm::error_ctx{.ec = injamm::error_code::unknown_key, "
         ".custom_error_message = \"unknown partial: \" + std::string(PartialName.data)});");
    --indent_;
    emit_raw("}");
    emit_raw("#endif // " + fallback_guard);
    emit_raw("");
  }

  /** @brief フッタ（名前空間クローズ + インクルードガード終了）を生成 */
  void emit_footer() {
    emit_raw("} // namespace " + namespace_);
    auto const guard = header_guard_name();
    emit_raw("");
    emit_raw("#endif // " + guard);
  }

  void precompute_control_ownership(bc::bytecode const& bc) {
    control_ownership_.clear();
    next_control_id_ = 0;
    struct control_block {
      int id;
      bool owns_value_scope;
    };
    std::vector<control_block> stack;
    auto parent_id = [&]() { return stack.empty() ? 0 : stack.back().id; };
    auto channel_for = [](int id) { return id == 0 ? std::string{"_root_control"} : "_control" + std::to_string(id); };
    auto push = [&](bc::instruction const& inst, block_syntax_kind syntax,
                    runtime_category runtime, bool owns_value_scope) {
      auto const id = ++next_control_id_;
      control_ownership_.emplace(&inst, control_ownership{
        .syntax = syntax,
        .runtime = runtime,
        .id = id,
        .parent_id = parent_id(),
        .loop_depth = scope_depth_,
        .local_channel = channel_for(id),
        .parent_channel = channel_for(parent_id()),
        .root_expression = root_for_emit(),
        .loop_context = loop_context_expr(),
      });
      stack.push_back({id, owns_value_scope});
      if (owns_value_scope) ++scope_depth_;
    };
    auto close = [&]() {
      if (stack.empty()) return;
      if (stack.back().owns_value_scope) --scope_depth_;
      stack.pop_back();
    };
    auto push_condition = [&](bc::instruction const& inst, runtime_category runtime) {
      auto const pid = parent_id();
      auto const id = ++next_control_id_;
      control_ownership_.emplace(&inst, control_ownership{
        .syntax = block_syntax_kind::condition,
        .runtime = runtime,
        .id = id,
        .parent_id = pid,
        .loop_depth = scope_depth_,
        .local_channel = channel_for(pid),
        .parent_channel = channel_for(pid),
        .root_expression = root_for_emit(),
        .loop_context = loop_context_expr(),
      });
      stack.push_back({pid, false});
    };
    scope_depth_ = 0;
    for (auto const& inst : bc.instructions) {
      switch (inst.op) {
        case bc::opcode::emit_section:
          push(inst, block_syntax_kind::section,
               bc.var_refs[inst.operand2].section_op_count > 0
                 ? runtime_category::container
                 : runtime_category::scalar_optional,
               true);
          break;
        case bc::opcode::emit_inverted:
          push(inst, block_syntax_kind::inverted, runtime_category::inverted, false);
          break;
        case bc::opcode::emit_if:
        case bc::opcode::emit_if_not:
        case bc::opcode::emit_if_or:
        case bc::opcode::emit_if_and:
        case bc::opcode::emit_if_eq:
        case bc::opcode::emit_if_ne:
        case bc::opcode::emit_if_gt:
        case bc::opcode::emit_if_gte:
        case bc::opcode::emit_if_lt:
        case bc::opcode::emit_if_lte:
        case bc::opcode::emit_if_filtered:
          push_condition(inst, runtime_category::if_branch);
          break;
        case bc::opcode::emit_at_section:
        case bc::opcode::emit_at_inverted:
          push_condition(inst, runtime_category::if_branch);
          break;
        case bc::opcode::emit_else:
          break;
        case bc::opcode::emit_end:
          close();
          break;
        case bc::opcode::emit_endif:
          close();
          break;
        default:
          break;
      }
    }
    scope_depth_ = 0;
  }

  /**
   * @brief ループ本体がインデックス変数 (_iN/_sizeN) を参照する emit_section を事前収集
   * @details @index/@first/@last/@size および loop.is_first/is_last 系
   *          (emit_at_section/emit_at_inverted) を本体に含むセクションを事前収集する。
   *          生成側は optional/スカラー分岐では親 context を継承し、コンテナ分岐だけを
   *          インデックス形式にする。ネストでは値スコープをすべて走査して、実際に
   *          コンテナとなる最内の分岐へ正しい VM ループ情報を渡す。
   * @param bc 解析対象のバイトコード
   */
  void precompute_index_loops(bc::bytecode const& bc) {
    index_loops_ = metadata_index_loops_;
    struct section_block {
      const bc::instruction* inst;
      bool is_section;
      bool in_else;
    };
    std::vector<section_block> stack;
    auto mark_enclosing_loops = [&]() {
      for (auto const& block : stack) {
        if (block.is_section && !block.in_else) index_loops_.insert(block.inst);
      }
    };
    for (auto const& inst : bc.instructions) {
      auto op = inst.op;
      if (op == bc::opcode::emit_section) {
        stack.push_back({&inst, true, false});
      }
      else if (op == bc::opcode::emit_inverted) {
        stack.push_back({&inst, false, false});
      }
      else if (op == bc::opcode::emit_at_index || op == bc::opcode::emit_at_index1 ||
               op == bc::opcode::emit_at_first || op == bc::opcode::emit_at_last ||
               op == bc::opcode::emit_at_size) {
        mark_enclosing_loops();
      }
      else if (op == bc::opcode::emit_at_section || op == bc::opcode::emit_at_inverted) {
        mark_enclosing_loops();
        stack.push_back({&inst, false, false});
      }
      else if (op == bc::opcode::emit_if || op == bc::opcode::emit_if_not ||
               op == bc::opcode::emit_if_or || op == bc::opcode::emit_if_and) {
        auto const& ref = bc.var_refs[inst.operand2];
        if (injamm::detail::classify_special_var(ref.key) != injamm::detail::special_var_kind::none) {
          mark_enclosing_loops();
        }
        if (op == bc::opcode::emit_if_or || op == bc::opcode::emit_if_and) {
          auto const& rhs = bc.var_refs[inst.operand3];
          if (injamm::detail::classify_special_var(rhs.key) != injamm::detail::special_var_kind::none) {
            mark_enclosing_loops();
          }
        }
        stack.push_back({&inst, false, false});
      }
      else if (op == bc::opcode::emit_if_eq || op == bc::opcode::emit_if_ne ||
               op == bc::opcode::emit_if_gt || op == bc::opcode::emit_if_gte ||
               op == bc::opcode::emit_if_lt || op == bc::opcode::emit_if_lte) {
        auto const& ref = bc.var_refs[inst.operand2];
        if (comparison_uses_loop_context(ref)) mark_enclosing_loops();
        stack.push_back({&inst, false, false});
      }
      else if (op == bc::opcode::emit_if_filtered) {
        stack.push_back({&inst, false, false});
      }
      else if (op == bc::opcode::call_partial) {
        mark_enclosing_loops();
      }
      else if (op == bc::opcode::emit_else) {
        if (!stack.empty() && stack.back().is_section) stack.back().in_else = true;
      }
      else if (op == bc::opcode::emit_end) {
        if (!stack.empty()) stack.pop_back();
      }
      else if (op == bc::opcode::emit_endif) {
        if (!stack.empty() && !stack.back().is_section) stack.pop_back();
      }
    }
  }

  /**
   * @brief バイトコードが _filtered バッファを使用するか判定
   * @details resolve_filtered が存在すれば以降のフィルタ命令も _filtered を使うため、
   *          この命令の有無だけで判定できる。
   * @param bc 判定対象のバイトコード
   * @return _filtered を使用する場合は true
   */
  bool uses_filtered(bc::bytecode const& bc) {
    for (auto const& inst : bc.instructions) {
      if (inst.op == bc::opcode::resolve_filtered) return true;
    }
    return false;
  }

  /**
   * @brief 補間値（変数・セクション内アイテム等）を出力する命令の数を数える
   * @details reserve の過小見積もりを防ぐため、リテラル長に加えて補間値の推定サイズを
   *          加算する。実行時の値サイズは不明なので、1値あたりの定数（k_interp_estimate）
   *          で安全な過大見積もりを行う。
   * @param bc 判定対象のバイトコード
   * @return 補間値出力命令の総数
   */
  std::size_t count_interpolations(bc::bytecode const& bc) {
    // ループ内の補間も1回として数える。reserve はヒントであり、
    // 実出力超過時は string が自動拡張される。過小見積もり（再確保）の回避が主目的。
    std::size_t n = 0;
    for (auto const& inst : bc.instructions) {
      switch (inst.op) {
        case bc::opcode::emit_var:
        case bc::opcode::emit_var_raw:
        case bc::opcode::emit_litvar:
        case bc::opcode::emit_litvar_raw:
        case bc::opcode::emit_at_root:
        case bc::opcode::emit_at_root_field:
        case bc::opcode::emit_at_root_field_raw:
        case bc::opcode::emit_at_key:
        case bc::opcode::emit_this:
        case bc::opcode::emit_filtered:
        case bc::opcode::emit_filtered_raw:
          ++n;
          break;
        default:
          break;
      }
    }
    return n;
  }

  /**
   * @brief 二段解決ヘルパの生成に必要なキーの収集と、ルート引数が必要なラムダの特定を行う
   * @details emit_header は render 関数本体の出力前に呼ばれるため、二段解決が
   *          必要なキーを出力前に確定させておく必要がある。エミッタと同じ規則で
   *          命令列を走査し（ブロック種別スタックでループ深度を追跡）、
   *          値スコープ相対参照になるキーを収集する。
   *
   *          あわせて、二段解決セクションのディスパッチ（if constexpr requires
   *          ガード）がジェネリックラムダ内に出力される場合、その else 側
   *          （ルート式.キー）がクロージャ構築時に検証されてコンパイルエラーに
   *          なる（Clang / MSVC）のを防ぐため、当該ディスパッチを含むラムダに
   *          ルート引数（_rtN）を持たせることをマーキングする。
   * @param bc 解析対象のバイトコード（partial 本体は再帰的に走査する）
   * @param in_partial partial 本体の走査か
   * @param depth 現在の値スコープ深度
   */
  void collect_two_stage_keys(bc::bytecode const& bc, bool in_partial, int depth) {
    struct pc_block {
      bool is_section;              /**< セクション（ラムダ + ループ深度を持つ）か */
      bool is_inv;                  /**< 逆セクション（ラムダを持つ）か */
      const bc::instruction* inst;  /**< 開始命令（マーキングのキー） */
      bool has_else;                /**< {{else}} に入った（深度を親に戻済み）か */
    };
    std::vector<pc_block> blocks;
    auto value_relative = [&](bc::var_ref const& ref) {
      if (ref.is_loop_parent ||
          injamm::detail::classify_special_var(ref.key) != injamm::detail::special_var_kind::none) {
        return false;
      }
      return in_partial || (depth > 0 && ref.root_fallback != injamm::detail::root_fb_proven);
    };
    /** 二段解決ディスパッチが属するラムダ（スタック上を外側へ探して最初のラムダ持ちブロック）にルート引数をマーキング */
    auto mark_enclosing_lambda = [&]() {
      for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
        if (it->is_section) {
          if (it->has_else) root_param_else_.insert(it->inst);
          else              root_param_main_.insert(it->inst);
          return;
        }
        if (it->is_inv) {
          root_param_inv_.insert(it->inst);
          return;
        }
      }
    };
    auto mark_enclosing_index_loop = [&]() {
      for (auto const& block : blocks) {
        if (block.is_section && !block.has_else) metadata_index_loops_.insert(block.inst);
      }
    };
    for (auto const& inst : bc.instructions) {
      switch (inst.op) {
        case bc::opcode::emit_section: {
          auto const& ref = bc.var_refs[inst.operand2];
          if (value_relative(ref)) {
            two_stage_keys_.insert(ref.key);
            mark_enclosing_lambda();  // ディスパッチはこの命令の emit_end に出力される
          }
          blocks.push_back({true, false, &inst, false});
          ++depth;
          break;
        }
        case bc::opcode::emit_inverted: {
          auto const& ref = bc.var_refs[inst.operand2];
          if (value_relative(ref)) two_stage_keys_.insert(ref.key);  // ヘルパ呼び出しのみ（マーキング不要）
          blocks.push_back({false, true, &inst, false});
          break;
        }
        case bc::opcode::resolve_filtered: {
          auto const& ref = bc.var_refs[inst.operand2];
          if (value_relative(ref)) two_stage_keys_.insert(ref.key);
          break;
        }
        case bc::opcode::emit_at_section:
        case bc::opcode::emit_at_inverted:
        case bc::opcode::emit_if_filtered:
          blocks.push_back({false, false, nullptr, false});
          break;
        case bc::opcode::emit_if:
        case bc::opcode::emit_if_not: {
          auto const& ref = bc.var_refs[inst.operand2];
          if (value_relative(ref)) two_stage_keys_.insert(ref.key);
          blocks.push_back({false, false, nullptr, false});
          break;
        }
        case bc::opcode::emit_if_eq:
        case bc::opcode::emit_if_ne:
        case bc::opcode::emit_if_gt:
        case bc::opcode::emit_if_gte:
        case bc::opcode::emit_if_lt:
        case bc::opcode::emit_if_lte: {
          auto const& ref = bc.var_refs[inst.operand2];
          if (value_relative(ref)) two_stage_keys_.insert(ref.key);
          if (static_cast<injamm::detail::compare_operand_kind>(ref.compare_rhs_kind) ==
              injamm::detail::compare_operand_kind::variable) {
            auto const rhs_ref = make_compare_rhs_ref(ref);
            if (value_relative(rhs_ref)) two_stage_keys_.insert(rhs_ref.key);
          }
          if (comparison_uses_loop_context(ref)) mark_enclosing_index_loop();
          blocks.push_back({false, false, nullptr, false});
          break;
        }
        case bc::opcode::emit_if_or:
        case bc::opcode::emit_if_and: {
          auto const& lhs = bc.var_refs[inst.operand2];
          auto const& rhs = bc.var_refs[inst.operand3];
          if (value_relative(lhs)) two_stage_keys_.insert(lhs.key);
          if (value_relative(rhs)) two_stage_keys_.insert(rhs.key);
          blocks.push_back({false, false, nullptr, false});
          break;
        }
        case bc::opcode::emit_else:
          if (!blocks.empty() && blocks.back().is_section) {
            blocks.back().has_else = true;
            --depth;  // else 本体は親コンテキスト（bytecode_compile.hpp と同一）
            if (depth < 0) depth = 0;
          }
          break;
        case bc::opcode::emit_end:
          if (!blocks.empty()) {
            auto blk = blocks.back();
            blocks.pop_back();
            if (blk.is_section && !blk.has_else) --depth;
            if (depth < 0) depth = 0;
          }
          break;
        case bc::opcode::emit_endif:
          if (!blocks.empty() && !blocks.back().is_section && !blocks.back().is_inv) blocks.pop_back();
          break;
        case bc::opcode::emit_var:
        case bc::opcode::emit_var_raw:
        case bc::opcode::emit_var_size:
          if (value_relative(bc.var_refs[inst.operand])) two_stage_keys_.insert(bc.var_refs[inst.operand].key);
          break;
        case bc::opcode::emit_litvar:
        case bc::opcode::emit_litvar_raw:
          if (value_relative(bc.var_refs[inst.operand2])) two_stage_keys_.insert(bc.var_refs[inst.operand2].key);
          break;
        default:
          break;
      }
    }
    for (auto const& pe : bc.partial_entries) {
      if (pe.bc) collect_two_stage_keys(*pe.bc, true, 0);
    }
  }

  void emit_section_dispatch(open_block const& block, bc::bytecode const& bc) {
    auto const control_type = cgns_name() + "::control_slot";
    auto const idx = std::to_string(block.idx);
    auto const local_slot = "_loop_control" + std::to_string(block.control_id);
    auto const body = "_body" + std::to_string(block.seq);
    auto const else_body = block.else_seq ? "_else" + std::to_string(block.else_seq) : std::string{};
    auto const root_args = block.root_param ? (", " + block.call_root) : std::string{};
    auto const scalar_call = [&](std::string const& item) {
      return body + "(" + item + ", " + block.call_loop + ", " + block.parent_control + root_args + ")";
    };
    auto const else_call = [&]() {
      if (else_body.empty()) return std::string{};
      auto call = else_body + "(" + block.parent_control;
      if (block.else_root_param) call += ", " + block.call_root;
      return call + ")";
    };
    auto const container_loop = [&](std::string const& access) {
      emit(control_type + " " + local_slot + ";");
      if (block.indexed) {
        emit("auto _size" + idx + " = " + access + ".size();");
        emit("for (std::size_t _i" + idx + " = 0; _i" + idx + " < _size" + idx + "; ++_i" + idx + ") {");
        ++indent_;
        emit(local_slot + " = {};");
        emit(body + "(" + access + "[_i" + idx + "], codegen_loop_context{true, _i" + idx +
             ", _size" + idx + "}, " + local_slot + root_args + ");");
        emit("if (" + local_slot + ".break_requested) break;");
        emit("if (" + local_slot + ".continue_requested) continue;");
        --indent_;
        emit("}");
      } else {
        emit("for (const auto& _v" + std::to_string(block.seq) + " : " + access + ") {");
        ++indent_;
        emit(local_slot + " = {};");
        emit(body + "(_v" + std::to_string(block.seq) + ", " + block.call_loop + ", " +
             local_slot + root_args + ");");
        emit("if (" + local_slot + ".break_requested) break;");
        emit("if (" + local_slot + ".continue_requested) continue;");
        --indent_;
        emit("}");
      }
    };
    auto dispatch_of = [&](std::string const& access) {
      emit("if constexpr (codegen_unsupported_string_v<decltype(" + access + ")>) {");
      ++indent_;
      emit("static_assert(!codegen_unsupported_string_v<decltype(" + access +
           ")>, \"raw char arrays and volatile C strings are not supported in codegen sections\");");
      --indent_;
      emit("} else if constexpr (codegen_string_section_v<decltype(" + access + ")>) {");
      ++indent_;
      emit("if (!value_empty(" + access + ")) {");
      ++indent_;
      emit(control_type + " " + local_slot + " = {};");
      auto const string_scope =
        block.idx > 1 ? "_item" + std::to_string(block.idx - 1) : (in_partial_ ? "value" : "data");
      emit(body + "(" + string_scope + ", codegen_loop_context{true, 0, 0}, " + local_slot +
           root_args + ");");
      --indent_;
      emit(std::string{"}"} + (block.else_seq ? " else {" : ""));
      if (block.else_seq) {
        ++indent_;
        emit(else_call() + ";");
        emit_control_stop(block.parent_control);
        --indent_;
        emit("}");
      }
      --indent_;
      emit("} else if constexpr (codegen_optional_section_v<decltype(" + access + ")>) {");
      ++indent_;
      emit("if (" + access + ".has_value()) {");
      ++indent_;
      emit(scalar_call("*" + access) + ";");
      emit_control_stop(block.parent_control);
      --indent_;
      emit(std::string{"}"} + (block.else_seq ? " else {" : ""));
      if (block.else_seq) {
        ++indent_;
        emit(else_call() + ";");
        emit_control_stop(block.parent_control);
        --indent_;
        emit("}");
      }
      --indent_;
      emit("} else if constexpr (codegen_scalar_section_v<decltype(" + access + ")>) {");
      ++indent_;
      emit("if (static_cast<bool>(" + access + ")) {");
      ++indent_;
      emit(scalar_call(
        (block.idx > 1 ? "_item" + std::to_string(block.idx - 1) : (in_partial_ ? "value" : "data"))) + ";");
      emit_control_stop(block.parent_control);
      --indent_;
      emit(std::string{"}"} + (block.else_seq ? " else {" : ""));
      if (block.else_seq) {
        ++indent_;
        emit(else_call() + ";");
        emit_control_stop(block.parent_control);
        --indent_;
        emit("}");
      }
      --indent_;
      emit("} else {");
      ++indent_;
      if (block.filtered) {
        emit_filtered_section(block, bc);
      } else if (block.else_seq) {
        emit("if (value_empty(" + access + ")) {");
        ++indent_;
        emit(else_call() + ";");
        emit_control_stop(block.parent_control);
        --indent_;
        emit("} else {");
        ++indent_;
        container_loop(access);
        --indent_;
        emit("}");
      } else {
        container_loop(access);
      }
      --indent_;
      emit("}");
    };
    dispatch_of(block.access);
  }

  void emit_filtered_section(open_block const& block, bc::bytecode const& bc) {
    auto const& ref = bc.var_refs[block.inst->operand2];
    auto const access = block.container_access.empty() ? block.access : block.container_access;
    auto const idx = std::to_string(block.idx);
    auto const control_type = cgns_name() + "::control_slot";
    auto const local_slot = "_loop_control" + std::to_string(block.control_id);
    auto const body = "_body" + std::to_string(block.seq);
    auto const else_body = block.else_seq ? "_else" + std::to_string(block.else_seq) : std::string{};
    auto const root_args = block.root_param ? (", " + block.call_root) : std::string{};
    auto const else_call = [&]() {
      if (else_body.empty()) return std::string{};
      auto call = else_body + "(" + block.parent_control;
      if (block.else_root_param) call += ", " + block.call_root;
      return call + ")";
    };
    bool has_stride = false;
    std::string join_separator;
    emit("{");
    ++indent_;
    emit("auto _size" + idx + " = " + access + ".size();");
    emit("std::size_t _lo" + idx + " = 0, _hi" + idx + " = _size" + idx + ";");
    emit("bool _bwd" + idx + " = false;");
    for (std::uint8_t si = 0; si < ref.section_op_count; ++si) {
      auto const& sop = ref.section_ops[si];
      auto const n = std::to_string(sop.arg);
      switch (sop.kind) {
        case injamm::detail::section_filter_op_kind::reverse:
          emit("_bwd" + idx + " = !_bwd" + idx + ";");
          break;
        case injamm::detail::section_filter_op_kind::take:
          emit("if (_bwd" + idx + ") _lo" + idx + " = (_hi" + idx + " > " + n +
               "u ? std::max(_lo" + idx + ", _hi" + idx + " - " + n + "u) : _lo" + idx + ");");
          emit("else _hi" + idx + " = std::min(_hi" + idx + ", _lo" + idx + " + " + n + "u);");
          break;
        case injamm::detail::section_filter_op_kind::skip:
          emit("if (_bwd" + idx + ") _hi" + idx + " = (_hi" + idx + " > " + n +
               "u ? _hi" + idx + " - " + n + "u : _lo" + idx + ");");
          emit("else _lo" + idx + " = std::min(_hi" + idx + ", _lo" + idx + " + " + n + "u);");
          break;
        case injamm::detail::section_filter_op_kind::take_last:
          emit("if (_bwd" + idx + ") _hi" + idx + " = std::min(_hi" + idx + ", _lo" + idx + " + " + n + "u);");
          emit("else _lo" + idx + " = (_hi" + idx + " > " + n +
               "u ? std::max(_lo" + idx + ", _hi" + idx + " - " + n + "u) : _lo" + idx + ");");
          break;
        case injamm::detail::section_filter_op_kind::skip_last:
          emit("if (_bwd" + idx + ") _lo" + idx + " = std::min(_hi" + idx + ", _lo" + idx + " + " + n + "u);");
          emit("else _hi" + idx + " = (_hi" + idx + " > " + n +
               "u ? _hi" + idx + " - " + n + "u : _lo" + idx + ");");
          break;
        case injamm::detail::section_filter_op_kind::stride:
          emit("const std::size_t _st_take" + idx + " = " + std::to_string(std::max(sop.arg, 0)) + "u;");
          emit("const std::size_t _st_skip" + idx + " = " + std::to_string(std::max(sop.arg2, 0)) + "u;");
          has_stride = true;
          break;
        case injamm::detail::section_filter_op_kind::join:
          join_separator.assign(sop.str_arg1.data(), sop.str_arg1.size());
          break;
      }
    }
    emit(control_type + " " + local_slot + ";");
    if (block.else_seq) {
      emit("if (value_empty(" + access + ")) {");
      ++indent_;
      emit(else_call() + ";");
      emit_control_stop(block.parent_control);
      --indent_;
      emit("} else {");
      ++indent_;
    }
    if (has_stride) {
      emit("const auto _block" + idx + " = _st_take" + idx + " + _st_skip" + idx + ";");
      emit("const auto _count" + idx + " = _st_take" + idx + " == 0 ? std::size_t(0) : "
           "((_hi" + idx + " - _lo" + idx + ") / _block" + idx + " * _st_take" + idx +
           " + std::min(_st_take" + idx + ", (_hi" + idx + " - _lo" + idx + ") % _block" + idx + "));");
      emit("std::size_t _src" + idx + " = _bwd" + idx + " ? (_hi" + idx + " - 1) : _lo" + idx + ";");
    } else {
      emit("const auto _count" + idx + " = _hi" + idx + " - _lo" + idx + ";");
    }
    emit("for (std::size_t _i" + idx + " = 0; _i" + idx + " < _count" + idx + "; ++_i" + idx + ") {");
    ++indent_;
    if (has_stride) {
      emit("while (!(_src" + idx + " >= _lo" + idx + " && _src" + idx + " < _hi" + idx +
           " && ((_bwd" + idx + " ? (_hi" + idx + " - 1 - _src" + idx +
           ") : (_src" + idx + " - _lo" + idx + ")) % _block" + idx + ") < _st_take" + idx +
           ")) { if (_bwd" + idx + ") --_src" + idx + "; else ++_src" + idx + "; }");
      emit("const auto& _item" + idx + " = " + access + "[_src" + idx + "];");
    } else {
      emit("const auto _idx" + idx + " = _bwd" + idx + " ? (_hi" + idx + " - 1 - _i" + idx +
           ") : (_lo" + idx + " + _i" + idx + ");");
      emit("const auto& _item" + idx + " = " + access + "[_idx" + idx + "];");
    }
    emit("const codegen_loop_context _loop" + idx + "{true, _i" + idx + ", _count" + idx + "};");
    emit(local_slot + " = {};");
    emit(body + "(_item" + idx + ", _loop" + idx + ", " + local_slot + root_args + ");");
    emit("if (" + local_slot + ".break_requested) break;");
    if (!join_separator.empty()) {
      emit("if (!" + local_slot + ".continue_requested && _i" + idx + " + 1 < _count" + idx +
           ") out.append(" + cpp_string(join_separator) + ");");
    }
    if (has_stride) {
      emit("if (_bwd" + idx + ") --_src" + idx + "; else ++_src" + idx + ";");
    }
    emit("if (" + local_slot + ".continue_requested) continue;");
    --indent_;
    emit("}");
    if (block.else_seq) {
      --indent_;
      emit("}");
    }
    --indent_;
    emit("}");
  }

  /**
   * @brief 個々のバイトコード命令を C++ コードに変換
   * @param inst 変換する命令
   * @param bc 含まれるバイトコード（リテラル・変数参照テーブルへのアクセス用）
   */
  void emit_instruction(bc::instruction const& inst, bc::bytecode const& bc) {
    auto op = inst.op;

    if (op == bc::opcode::emit_literal) {
      emit("out.append(" + cpp_string(bc.literals[inst.operand]) + ");");
    }
    else if (op == bc::opcode::emit_var) {
      emit_value_ref("html_escape_append_value(out, ", ");", bc.var_refs[inst.operand]);
    }
    else if (op == bc::opcode::emit_var_raw) {
      emit_value_ref("append_value(out, ", ");", bc.var_refs[inst.operand]);
    }
    else if (op == bc::opcode::emit_section) {
      auto const& ref = bc.var_refs[inst.operand2];
      auto access = resolve_value_access(ref);
      auto container_access = resolve_value_access(ref);
      auto const call_loop = loop_context_expr();
      auto const ownership = control_ownership_.at(&inst);
      ++scope_depth_;
      auto idx = std::to_string(scope_depth_);
      auto seq = ++lambda_seq_;
      bool rpm = root_param_main_.count(&inst) != 0;
      std::string params = "(const auto& _item" + idx + ", const codegen_loop_context& _loop" + idx +
                           ", " + cgns_name() + "::control_slot& " + ownership.local_channel;
      std::string call_root = root_for_emit();
      if (rpm) {
        params += ", const auto& _rt" + std::to_string(seq);
        cur_root_ = "_rt" + std::to_string(seq);
      }
      emit("auto _body" + std::to_string(seq) + " = [&]" + params + ") {");
      ++indent_;
      open_block blk{
        .syntax = ownership.syntax,
        .phase = block_phase::main,
        .runtime = ownership.runtime,
        .control_id = ownership.id,
        .local_control = ownership.local_channel,
        .parent_control = ownership.parent_channel,
        .root_expression = call_root,
        .loop_context = ownership.loop_context,
        .loop_depth = scope_depth_,
        .access = access,
        .container_access = container_access,
        .idx = scope_depth_,
        .seq = seq,
        .root_param = rpm,
        .call_root = call_root,
        .call_loop = call_loop,
        .indexed = index_loops_.count(&inst) != 0,
        .inst = &inst,
        .filtered = ref.section_op_count > 0,
      };
      open_blocks_.push_back(std::move(blk));
    }
    else if (op == bc::opcode::emit_at_section) {
      auto kind = inst.operand2;
      auto const loop = loop_context_expr();
      if (kind == 0) {
        emit("if (" + loop + ".active && " + loop + ".index > 0) {");
      } else if (kind == 1) {
        emit("if (" + loop + ".active && " + loop + ".index == 0) {");
      } else if (kind == 2) {
        emit("if (" + loop + ".active && " + loop + ".index + 1 == " + loop + ".size) {");
      } else {
        emit("if (false) {");
      }
      ++indent_;
      auto const& ownership = control_ownership_.at(&inst);
      open_blocks_.push_back({
        .syntax = block_syntax_kind::at_condition,
        .runtime = runtime_category::if_branch,
        .control_id = ownership.id,
        .local_control = ownership.local_channel,
        .parent_control = ownership.parent_channel,
        .root_expression = ownership.root_expression,
        .loop_context = ownership.loop_context,
        .loop_depth = ownership.loop_depth,
      });
    }
    else if (op == bc::opcode::emit_at_inverted) {
      auto kind = inst.operand2;
      auto const loop = loop_context_expr();
      if (kind == 0) {
        emit("if (!" + loop + ".active || " + loop + ".index == 0) {");
      } else if (kind == 1) {
        emit("if (!" + loop + ".active || " + loop + ".index > 0) {");
      } else if (kind == 2) {
        emit("if (!" + loop + ".active || " + loop + ".index + 1 != " + loop + ".size) {");
      } else {
        emit("if (false) {");
      }
      ++indent_;
      auto const& ownership = control_ownership_.at(&inst);
      open_blocks_.push_back({
        .syntax = block_syntax_kind::at_condition,
        .runtime = runtime_category::if_branch,
        .control_id = ownership.id,
        .local_control = ownership.local_channel,
        .parent_control = ownership.parent_channel,
        .root_expression = ownership.root_expression,
        .loop_context = ownership.loop_context,
        .loop_depth = ownership.loop_depth,
      });
    }
    else if (op == bc::opcode::emit_end) {
      if (open_blocks_.empty()) return;
      auto block = std::move(open_blocks_.back());
      open_blocks_.pop_back();
      if (block.syntax == block_syntax_kind::condition ||
          block.syntax == block_syntax_kind::at_condition) {
        --indent_;
        emit("}");
        return;
      }
      if (block.syntax == block_syntax_kind::section) {
        cur_root_ = block.call_root;  // ディスパッチはラムダの外側スコープに出力される
        --indent_;
        emit("};");
        emit_section_dispatch(block, bc);
        if (block.phase == block_phase::main) --scope_depth_;
        return;
      }
      if (block.syntax == block_syntax_kind::inverted) {
        cur_root_ = block.call_root;
        --indent_;
        emit("};");
        auto body = "_body" + std::to_string(block.seq);
        auto body_call = body + "(" + block.parent_control;
        if (block.root_param) body_call += ", " + block.call_root;
        body_call += ")";
        auto els = block.else_seq ? "_else" + std::to_string(block.else_seq) : std::string{};
        auto els_call = block.else_seq ? (els + "(" + block.parent_control +
                         (block.else_root_param ? (", " + block.call_root) : "") + ")") : std::string{};
        if (!block.else_seq) {
          emit("if (value_empty(" + block.access + ")) {");
          ++indent_;
          emit(body_call + ";");
          emit_control_stop(block.parent_control);
          --indent_;
          emit("}");
        } else {
          emit("if (value_empty(" + block.access + ")) {");
          ++indent_;
          emit(body_call + ";");
          emit_control_stop(block.parent_control);
          --indent_;
          emit("} else {");
          ++indent_;
          emit(els_call + ";");
          emit_control_stop(block.parent_control);
          --indent_;
          emit("}");
        }
      }
    }
    else if (op == bc::opcode::emit_inverted) {
      /** 逆セクション: 本体をラムダに 1 回だけ書き、emit_end で value_empty 分岐を
       *  出す（セクションと同じ構造）。{{else}} は emit_else で else ラムダにする。
       *  アクセスが値スコープ相対の場合は二段解決ヘルパ経由（文なので安全）。 */
      auto const& ref = bc.var_refs[inst.operand2];
      auto access = resolve_access(ref);
      if (is_value_relative(ref)) access = two_stage_access(ref.key);
      auto seq = ++lambda_seq_;
      bool rpm = root_param_inv_.count(&inst) != 0;
      std::string call_root = root_for_emit();
      auto const& ownership = control_ownership_.at(&inst);
      std::string params = "(" + cgns_name() + "::control_slot& " + ownership.local_channel;
      if (rpm) {
        params += ", const auto& _rt" + std::to_string(seq);
        cur_root_ = "_rt" + std::to_string(seq);
      }
      emit("auto _body" + std::to_string(seq) + " = [&]" + params + ") {");
      ++indent_;
      open_block blk{
        .syntax = block_syntax_kind::inverted,
        .runtime = runtime_category::inverted,
        .control_id = ownership.id,
        .local_control = ownership.local_channel,
        .parent_control = ownership.parent_channel,
        .root_expression = call_root,
        .loop_context = ownership.loop_context,
        .loop_depth = ownership.loop_depth,
        .access = access,
        .idx = scope_depth_,
        .seq = seq,
        .root_param = rpm,
        .call_root = call_root,
        .inst = &inst,
      };
      open_blocks_.push_back(std::move(blk));
    }
    else if (op == bc::opcode::emit_at_index) {
      auto const loop = loop_context_expr();
      emit("if (" + loop + ".active) append_number(out, " + loop + ".index);");
    }
    else if (op == bc::opcode::emit_at_index1) {
      auto const loop = loop_context_expr();
      emit("if (" + loop + ".active) append_number(out, " + loop + ".index + 1);");
    }
    else if (op == bc::opcode::emit_at_first) {
      auto const loop = loop_context_expr();
      emit("out.append((" + loop + ".active && " + loop + ".index == 0) ? \"true\" : \"false\");");
    }
    else if (op == bc::opcode::emit_at_last) {
      auto const loop = loop_context_expr();
      emit("out.append((" + loop + ".active && " + loop + ".index + 1 == " + loop + ".size) ? \"true\" : \"false\");");
    }
    else if (op == bc::opcode::emit_at_size) {
      auto const loop = loop_context_expr();
      emit("if (" + loop + ".active) append_number(out, " + loop + ".size);");
    }
    else if (op == bc::opcode::emit_if) {
      auto access = resolve_if_access(bc.var_refs[inst.operand2]);
      emit("if (static_cast<bool>(" + access + ")) {");
      ++indent_;
      auto const& ownership = control_ownership_.at(&inst);
      open_blocks_.push_back({
        .syntax = block_syntax_kind::condition,
        .runtime = runtime_category::if_branch,
        .control_id = ownership.id,
        .local_control = ownership.local_channel,
        .parent_control = ownership.parent_channel,
        .root_expression = ownership.root_expression,
        .loop_context = ownership.loop_context,
        .loop_depth = ownership.loop_depth,
      });
    }
    else if (op == bc::opcode::emit_if_eq || op == bc::opcode::emit_if_ne ||
             op == bc::opcode::emit_if_gt || op == bc::opcode::emit_if_gte ||
             op == bc::opcode::emit_if_lt || op == bc::opcode::emit_if_lte) {
      auto const& ref = bc.var_refs[inst.operand2];
      auto const lhs = resolve_compare_lhs(ref);
      auto const rhs = get_compare_rhs_expression(ref);
      auto const rhs_kind = compare_rhs_kind_expression(ref);
      auto const needs_loop_guard =
          comparison_uses_loop_context(ref) || comparison_rhs_uses_loop_context(ref);
      auto const guard = needs_loop_guard ? (loop_context_expr() + ".active && ") : std::string{};
      emit("if (" + guard + "codegen_compare(" + compare_opcode_expression(op) + ", " +
           rhs_kind + ", " + lhs + ", " + rhs + ")) {");
      ++indent_;
      auto const& ownership = control_ownership_.at(&inst);
      open_blocks_.push_back({
        .syntax = block_syntax_kind::condition,
        .runtime = runtime_category::if_branch,
        .control_id = ownership.id,
        .local_control = ownership.local_channel,
        .parent_control = ownership.parent_channel,
        .root_expression = ownership.root_expression,
        .loop_context = ownership.loop_context,
        .loop_depth = ownership.loop_depth,
      });
    }
    else if (op == bc::opcode::emit_else) {
      if (!open_blocks_.empty() && open_blocks_.back().syntax == block_syntax_kind::section) {
        auto& blk = open_blocks_.back();
        if (blk.root_param) cur_root_ = blk.call_root;
        --indent_;
        emit("};");
        --scope_depth_;
        blk.else_seq = ++lambda_seq_;
        blk.phase = block_phase::else_body;
        blk.else_root_param = blk.inst && root_param_else_.count(blk.inst) != 0;
        blk.local_control = "_control_else" + std::to_string(blk.control_id);
        std::string params = "(" + cgns_name() + "::control_slot& " + blk.local_control;
        if (blk.else_root_param) {
          cur_root_ = "_rt" + std::to_string(blk.else_seq);
          params += ", const auto& _rt" + std::to_string(blk.else_seq);
        }
        emit("auto _else" + std::to_string(blk.else_seq) + " = [&]" + params + ") {");
        ++indent_;
      } else if (!open_blocks_.empty() && open_blocks_.back().syntax == block_syntax_kind::inverted) {
        auto& blk = open_blocks_.back();
        if (blk.root_param) cur_root_ = blk.call_root;
        --indent_;
        emit("};");
        blk.else_seq = ++lambda_seq_;
        blk.phase = block_phase::else_body;
        blk.else_root_param = blk.root_param;
        blk.local_control = "_control_else" + std::to_string(blk.control_id);
        std::string params = "(" + cgns_name() + "::control_slot& " + blk.local_control;
        if (blk.root_param) {
          cur_root_ = "_rt" + std::to_string(blk.else_seq);
          params += ", const auto& _rt" + std::to_string(blk.else_seq);
        }
        emit("auto _else" + std::to_string(blk.else_seq) + " = [&]" + params + ") {");
        ++indent_;
      } else {
        --indent_;
        emit("} else {");
        ++indent_;
        if (!open_blocks_.empty()) open_blocks_.back().phase = block_phase::else_body;
      }
    }
    else if (op == bc::opcode::emit_endif) {
      if (!open_blocks_.empty() &&
          (open_blocks_.back().syntax == block_syntax_kind::condition ||
           open_blocks_.back().syntax == block_syntax_kind::at_condition)) {
        open_blocks_.pop_back();
      }
      --indent_;
      emit("}");
    }
    else if (op == bc::opcode::emit_litvar) {
      emit("out.append(" + cpp_string(bc.literals[inst.operand]) + ");");
      emit_value_ref("html_escape_append_value(out, ", ");", bc.var_refs[inst.operand2]);
    }
    else if (op == bc::opcode::emit_litvar_raw) {
      emit("out.append(" + cpp_string(bc.literals[inst.operand]) + ");");
      emit_value_ref("append_value(out, ", ");", bc.var_refs[inst.operand2]);
    }
    else if (op == bc::opcode::emit_at_root) {
      emit("html_escape_append_value(out, " + root_expr() + ");");
    }
    else if (op == bc::opcode::emit_at_root_field) {
      auto const& key = bc.var_refs[inst.operand].key;
      if (key.size() > 5 && key.rfind(".size") == key.size() - 5) {
        emit("if constexpr (requires { " + root_expr() + "." + key + "; }) {");
        ++indent_;
        emit("html_escape_append_value(out, " + root_expr() + "." + key + ");");
        --indent_;
        emit("}");
      } else {
        emit("html_escape_append_value(out, " + root_expr() + "." + key + ");");
      }
    }
    else if (op == bc::opcode::emit_at_root_field_raw) {
      auto const& key = bc.var_refs[inst.operand].key;
      if (key.size() > 5 && key.rfind(".size") == key.size() - 5) {
        emit("if constexpr (requires { " + root_expr() + "." + key + "; }) {");
        ++indent_;
        emit("append_value(out, " + root_expr() + "." + key + ");");
        --indent_;
        emit("}");
      } else {
        emit("append_value(out, " + root_expr() + "." + key + ");");
      }
    }
    else if (op == bc::opcode::emit_at_key) {
      emit("out.append(_key" + std::to_string(scope_depth_) + ");");
    }
    else if (op == bc::opcode::emit_this) {
      if (scope_depth_ > 0) {
        emit("html_escape_append_value(out, _item" + std::to_string(scope_depth_) + ");");
      } else {
        emit("html_escape_append_value(out, " + value_expr() + ");");
      }
    }
    else if (op == bc::opcode::emit_filtered) {
      emit("html_escape_append(out, _filtered);");
    }
    else if (op == bc::opcode::emit_filtered_raw) {
      emit("out.append(_filtered);");
    }
    else if (op == bc::opcode::resolve_filtered) {
      auto const& ref = bc.var_refs[inst.operand2];
      auto access = resolve_value_access(ref);
      bool use_json = (ref.filter_flags & 1) != 0;
      cur_filter_ref_ = &ref;
      filter_str_pos_ = 0;
      // format フィルタは型付き値に適用する（VM の do_resolve_filtered と同様）
      bool use_format = (ref.filter_flags & 2) != 0;
      std::string fmt_str;
      if (use_format) {
        for (auto const& f : ref.filters) {
          if (f.filter == static_cast<std::uint8_t>(injamm::detail::string_filter::format)) { fmt_str = f.str_arg1; break; }
        }
      }
      if (use_json) {
        // serializable な型は serialize_value、reflectable な型は glz::write_json を使う
        emit("if constexpr (::injamm::detail::serializable_v<std::remove_cvref_t<decltype(" + access + ")>>) {");
        ++indent_;
        emit("_filtered.clear(); ::injamm::detail::serialize_value(_filtered, " + access + ");");
        --indent_;
        emit("} else if constexpr (::injamm::detail::ct_glz_reflectable<std::remove_cvref_t<decltype(" + access + ")>>) {");
        ++indent_;
        emit("(void)::glz::write_json(" + access + ", _filtered);");
        --indent_;
        emit("} else {");
        ++indent_;
        // assign(const string&) は内部で size() を 2回呼ぶので
        // (data, size) 版で 1 回に減らす。
        emit("_filtered.assign((" + access + ").data(), (" + access + ").size());");
        --indent_;
        emit("}");
      } else {
        if (use_format) {
          emit("if constexpr (::injamm::detail::is_chrono_time_point_v<std::remove_cvref_t<decltype(" + access + ")>>) {");
          ++indent_;
          emit("_filtered.clear(); ::injamm::detail::serialize_chrono(_filtered, " + access + ", " + cpp_string(fmt_str) + ");");
          --indent_;
          emit("} else if constexpr (std::is_arithmetic_v<std::remove_cvref_t<decltype(" + access + ")>> && !std::is_same_v<std::remove_cvref_t<decltype(" + access + ")>, bool>) {");
          ++indent_;
          emit("_filtered.clear(); (void)::injamm::detail::serialize_formatted(_filtered, " + access + ", " + cpp_string(fmt_str) + ");");
          --indent_;
          emit("} else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(" + access + ")>, std::string> || std::is_same_v<std::remove_cvref_t<decltype(" + access + ")>, std::string_view>) {");
          ++indent_;
          emit("_filtered.clear(); (void)::injamm::detail::serialize_formatted(_filtered, ::injamm::detail::to_sv(" + access + "), " + cpp_string(fmt_str) + ");");
          --indent_;
          emit("} else ");
        }
        // serializable な型は serialize_value、reflectable な型は glz::write_json、
        // それ以外は assign にフォールバック
        emit("if constexpr (::injamm::detail::serializable_v<std::remove_cvref_t<decltype(" + access + ")>>) {");
        ++indent_;
        emit("_filtered.clear(); ::injamm::detail::serialize_value(_filtered, " + access + ");");
        --indent_;
        emit("} else if constexpr (::injamm::detail::ct_glz_reflectable<std::remove_cvref_t<decltype(" + access + ")>>) {");
        ++indent_;
        emit("(void)::glz::write_json(" + access + ", _filtered);");
        --indent_;
        emit("} else {");
        ++indent_;
        // assign(const string&) は内部で size() を 2回呼ぶので
        // (data, size) 版で 1 回に減らす。
        emit("_filtered.assign((" + access + ").data(), (" + access + ").size());");
        --indent_;
        emit("}");
      }
    }
    else if (op == bc::opcode::filter_string) {
      // 汎用文字列フィルタ。文字列引数は VM と同様 var_ref.filters から読む
      // （operand3 は substr の arg2 を除き未使用）。
      auto const kind = static_cast<injamm::detail::string_filter>(inst.operand2);
      auto const arg = std::to_string(inst.operand);
      std::string lit1, lit2;
      if (cur_filter_ref_ && filter_str_pos_ < cur_filter_ref_->filters.size()) {
        auto const& fe = cur_filter_ref_->filters[filter_str_pos_++];
        lit1 = cpp_string(fe.str_arg1);
        lit2 = cpp_string(fe.str_arg2);
      } else {
        ++filter_str_pos_;
      }
      switch (kind) {
      case injamm::detail::string_filter::upper:      emit("filter_to_upper(_filtered);"); break;
      case injamm::detail::string_filter::lower:      emit("filter_to_lower(_filtered);"); break;
      case injamm::detail::string_filter::capitalize: emit("filter_capitalize(_filtered);"); break;
      case injamm::detail::string_filter::title:      emit("filter_title(_filtered);"); break;
      case injamm::detail::string_filter::trim:       emit("filter_trim(_filtered, " + (lit1.empty() ? "\"\"" : lit1) + ");"); break;
      case injamm::detail::string_filter::ltrim:      emit("filter_ltrim(_filtered, " + (lit1.empty() ? "\"\"" : lit1) + ");"); break;
      case injamm::detail::string_filter::rtrim:      emit("filter_rtrim(_filtered, " + (lit1.empty() ? "\"\"" : lit1) + ");"); break;
      case injamm::detail::string_filter::left:       emit("filter_left(_filtered, " + arg + ");"); break;
      case injamm::detail::string_filter::right:      emit("filter_right(_filtered, " + arg + ");"); break;
      case injamm::detail::string_filter::center:     emit("filter_center(_filtered, " + arg + ");"); break;
      case injamm::detail::string_filter::truncate:   emit("filter_truncate(_filtered, " + arg + ");"); break;
      case injamm::detail::string_filter::substr:     emit("filter_substr(_filtered, " + arg + ", " + std::to_string(inst.operand3) + ");"); break;
      case injamm::detail::string_filter::replace:    emit("filter_replace(_filtered, " + (lit1.empty() ? "\"\"" : lit1) + ", " + (lit2.empty() ? "\"\"" : lit2) + ");"); break;
      case injamm::detail::string_filter::default_value: emit("filter_default(_filtered, " + (lit1.empty() ? "\"\"" : lit1) + ");"); break;
      case injamm::detail::string_filter::to_json:    break; // resolve_filtered で処理済み
      case injamm::detail::string_filter::safe:       break; // safe: no-op
      case injamm::detail::string_filter::indent:     emit("filter_indent(_filtered, " + arg + ");"); break;
      case injamm::detail::string_filter::pad:        emit("filter_pad(_filtered, " + arg + ", " + (lit1.empty() ? "\" \"" : lit1) + ");"); break;
      case injamm::detail::string_filter::pluralize:  emit("filter_pluralize(_filtered, " + (lit1.empty() ? "\"\"" : lit1) + ", " + (lit2.empty() ? "\"\"" : lit2) + ");"); break;
      case injamm::detail::string_filter::format:     emit("filter_format(_filtered);"); break;
      case injamm::detail::string_filter::repeat:     emit("filter_repeat(_filtered, " + arg + ");"); break;
      }
    }
    else if (op == bc::opcode::filter_int) {
      // 汎用整数フィルタ: operand2 = int_filter 種別, operand = arg
      auto const kind = static_cast<injamm::detail::int_filter>(inst.operand2);
      auto const arg = std::to_string(inst.operand);
      switch (kind) {
      case injamm::detail::int_filter::abs:     emit("filter_int_abs(_filtered);"); break;
      case injamm::detail::int_filter::hex:     emit("filter_int_hex(_filtered);"); break;
      case injamm::detail::int_filter::oct:     emit("filter_int_oct(_filtered);"); break;
      case injamm::detail::int_filter::bin:     emit("filter_int_bin(_filtered);"); break;
      case injamm::detail::int_filter::neg:     emit("filter_int_neg(_filtered);"); break;
      case injamm::detail::int_filter::mod:     emit("filter_int_mod(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::numify:  emit("filter_int_numify(_filtered);"); break;
      case injamm::detail::int_filter::is_neg:  emit("filter_int_is_neg(_filtered);"); break;
      case injamm::detail::int_filter::eq:      emit("filter_int_eq(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::ne:      emit("filter_int_ne(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::gt:      emit("filter_int_gt(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::gte:     emit("filter_int_gte(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::lt:      emit("filter_int_lt(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::lte:     emit("filter_int_lte(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::zerofill: emit("filter_int_zerofill(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::add:     emit("filter_int_add(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::sub:     emit("filter_int_sub(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::mul:     emit("filter_int_mul(_filtered, " + arg + ");"); break;
      case injamm::detail::int_filter::div:     emit("filter_int_div(_filtered, " + arg + ");"); break;
      }
    }
    else if (op == bc::opcode::filter_float) {
      auto const kind = static_cast<injamm::detail::float_filter>(inst.operand2);
      switch (kind) {
        case injamm::detail::float_filter::precision:
          emit("filter_float_precision(_filtered, " + std::to_string(inst.operand) + ");");
          break;
        case injamm::detail::float_filter::round:
          emit("filter_float_round(_filtered, " + std::to_string(inst.operand) + ");");
          break;
      }
    }
    else if (op == bc::opcode::emit_if_filtered) {
      // VM (bytecode_exec.hpp handle_emit_if_filtered) と同じ真偽判定
      emit("if (!_filtered.empty() && _filtered != \"false\" && _filtered != \"0\") {");
      ++indent_;
      auto const& ownership = control_ownership_.at(&inst);
      open_blocks_.push_back({.syntax = block_syntax_kind::condition, .runtime = runtime_category::if_branch,
        .control_id = ownership.id, .local_control = ownership.local_channel,
        .parent_control = ownership.parent_channel, .root_expression = ownership.root_expression,
        .loop_context = ownership.loop_context, .loop_depth = ownership.loop_depth});
    }
    else if (op == bc::opcode::emit_if_or || op == bc::opcode::emit_if_and) {
      auto lhs = resolve_if_access(bc.var_refs[inst.operand2]);
      auto rhs = resolve_if_access(bc.var_refs[inst.operand3]);
      emit("if ((static_cast<bool>(" + lhs + ")) " + (op == bc::opcode::emit_if_or ? "||" : "&&") + " (static_cast<bool>(" + rhs + "))) {");
      ++indent_;
      auto const& ownership = control_ownership_.at(&inst);
      open_blocks_.push_back({.syntax = block_syntax_kind::condition, .runtime = runtime_category::if_branch,
        .control_id = ownership.id, .local_control = ownership.local_channel,
        .parent_control = ownership.parent_channel, .root_expression = ownership.root_expression,
        .loop_context = ownership.loop_context, .loop_depth = ownership.loop_depth});
    }
    else if (op == bc::opcode::emit_if_not) {
      auto access = resolve_if_access(bc.var_refs[inst.operand2]);
      emit("if (!static_cast<bool>(" + access + ")) {");
      ++indent_;
      auto const& ownership = control_ownership_.at(&inst);
      open_blocks_.push_back({.syntax = block_syntax_kind::condition, .runtime = runtime_category::if_branch,
        .control_id = ownership.id, .local_control = ownership.local_channel,
        .parent_control = ownership.parent_channel, .root_expression = ownership.root_expression,
        .loop_context = ownership.loop_context, .loop_depth = ownership.loop_depth});
    }
    else if (op == bc::opcode::call_partial) {
      /* partial 呼び出し: emit_partial_dispatch で生成された render_partial_impl に
         現在値とルートを渡す（VM handle_call_partial と同一）。
         partial 本体は値 → ルートの二段解決で生成されているため、ループ内から呼んだ
         グローバル partial も現在の要素を優先して解決される（従来はルート固定で
         VM と結果が不一致だった）。local partial も同じ経路で描画される。 */
      auto& pe = bc.partial_entries[inst.operand];
      if (pe.bc && !pe.bc->instructions.empty()) {
        auto const result = "_partial_result" + std::to_string(++lambda_seq_);
        emit("auto " + result + " = " + partial_impl_name() + "<\"" + pe.name + "\">(" +
             value_expr() + ", " + root_expr() + ", " + loop_context_expr() + ", " +
             active_control_channel() + ", out);");
        emit("if (!" + result + ") " +
             (in_control_lambda() ? "return;" :
                                  ("return std::unexpected(" + result + ".error());")));
        emit_control_stop(active_control_channel());
      }
    }
    else if (op == bc::opcode::emit_var_size) {
      emit_value_ref("append_number(out, value_size(", "));", bc.var_refs[inst.operand]);
    }
    else if (op == bc::opcode::emit_break) {
      emit(active_control_channel() + ".break_requested = true;");
      emit(in_control_lambda() ? "return;" : "return {};");
    }
    else if (op == bc::opcode::emit_continue) {
      emit(active_control_channel() + ".continue_requested = true;");
      emit(in_control_lambda() ? "return;" : "return {};");
    }
    else if (op == bc::opcode::halt) {
      // 何もしない
    }
    else {
      emit("// TODO: opcode " + std::to_string(static_cast<int>(op)));
    }
  }

public:
  /**
   * @brief コンストラクタ
   * @param type_name データ型名（コメント用）
   * @param ns 生成コードの名前空間
   * @param func_prefix 関数名プレフィックス（デフォルト: 空 = "render"）
   */
  code_generator(std::string type_name, std::string ns, std::string func_prefix = "", bool no_simd = false)
    : type_name_(std::move(type_name)), namespace_(std::move(ns)), func_prefix_(std::move(func_prefix)), no_simd_(no_simd) {}

  /** @brief generate() が対応不能な命令を検出した場合の説明（成功時は空文字列） */
  [[nodiscard]] std::string const& unsupported_feature() const noexcept { return unsupported_feature_; }

  /**
   * @brief codegen が対応していない命令がバイトコードに含まれるか検出する
   * @return 対応不能な命令の名前。すべて対応可能なら空文字列
   */
  static std::string find_unsupported(bc::bytecode const& bc) {
    std::function<std::string(bc::bytecode const&)> scan = [&](bc::bytecode const& b) -> std::string {
      for (auto const& inst : b.instructions) {
        if (inst.op == bc::opcode::emit_at_key) {
          /** {{loop.key}}（マップ反復キー）: 生成コードに _keyN 変数の宣言が存在しないため
           *  コンパイル不能なヘッダになる。 */
          return "{{loop.key}} (map iteration key)";
        }
      }
      for (auto const& ref : b.var_refs) {
        /** {{@key}} 等の @ 変数: emit_var 経由で "data.@key" という不正なメンバアクセスに
         *  なるため拒否する（旧実装はコンパイル不能なヘッダを書き出したまま exit 0 だった）。 */
        if (!ref.key.empty() && ref.key[0] == '@') {
          return "{{" + ref.key + "}} (@ 変数は codegen 未対応)";
        }
      }
      for (auto const& pe : b.partial_entries) {
        if (!pe.bc) continue;
        auto nested = scan(*pe.bc);
        if (!nested.empty()) return nested;
      }
      return {};
    };
    return scan(bc);
  }

  /**
   * @brief バイトコードから C++ コードを生成
   * @param bc 入力バイトコード
   * @return 生成された C++ ヘッダファイルの内容。対応不能な命令を含む場合は nullopt
   */
  std::optional<std::string> generate(bc::bytecode const& bc) {
    if (auto unsupported = find_unsupported(bc); !unsupported.empty()) {
      unsupported_feature_ = unsupported;
      return std::nullopt;
    }
    /** 二段解決ヘルパのキーを出力前に確定する（ヘルパは使用箇所より前に生成が必要） */
    two_stage_keys_.clear();
    metadata_index_loops_.clear();
    collect_two_stage_keys(bc, false, 0);
    // リテラルの合計サイズ + 補間値の推定サイズで reserve を過大見積もりする
    std::size_t total_literal_size = 0;
    for (auto const& lit : bc.literals) {
      total_literal_size += lit.size();
    }
    constexpr std::size_t interp_estimate = 32;   // 補間値1つあたりの推定バイト数
    // SSO 容量（libstdc++=15, MSVC=22 をカバーする最小値）。このサイズ以下の出力は
    // std::string の SSO でヒープ確保なしで収まるため、reserve 自体を省略できる。
    constexpr std::size_t sso_capacity = 15;
    std::size_t reserve_size = total_literal_size + count_interpolations(bc) * interp_estimate;
    // SSO に収まる見込みの小テンプレートは reserve を省略し、ヒープ確保を避ける。
    // 各補間値が最大 SSO 容量まで出力し得ると仮定しても全体が SSO に収まる場合のみ
    // 省略する（リテラルのみで 15 バイト以下、または補間1つのみでリテラルなし）。
    if (total_literal_size + count_interpolations(bc) * sso_capacity <= sso_capacity) {
      reserve_size = 0;
    }

    emit_header();
    /* partial ディスパッチ関数を先に出力: メイン関数から render_partial<"name"> として
       呼び出せるようにする。partial がない場合は何も出力しない。 */
    emit_partial_dispatch(bc);
    emit_render_into_start(reserve_size, uses_filtered(bc));

    /* range-for 化の判定をループ命令の生成前に済ませる */
    precompute_control_ownership(bc);
    precompute_index_loops(bc);

    // 隣接リテラルを結合して出力
    std::string accumulated_literals;
    auto flush_literals = [&]() {
      if (!accumulated_literals.empty()) {
        emit("out.append(" + cpp_string(accumulated_literals) + ");");
        accumulated_literals.clear();
      }
    };

    for (auto const& inst : bc.instructions) {
      auto op = inst.op;
      if (op == bc::opcode::emit_literal) {
        // リテラルを蓄積（結合待機）
        accumulated_literals += bc.literals[inst.operand];
        continue;
      }
      if (op == bc::opcode::emit_litvar) {
        // 蓄積リテラル + 変数の融合命令
        accumulated_literals += bc.literals[inst.operand];
        flush_literals();
        emit_value_ref("html_escape_append_value(out, ", ");", bc.var_refs[inst.operand2]);
        continue;
      }
      if (op == bc::opcode::emit_litvar_raw) {
        accumulated_literals += bc.literals[inst.operand];
        flush_literals();
        emit_value_ref("append_value(out, ", ");", bc.var_refs[inst.operand2]);
        continue;
      }
      // 他の命令の前に蓄積リテラルをフラッシュ
      flush_literals();
      emit_instruction(inst, bc);
    }
    // 最後の蓄積リテラルをフラッシュ
    flush_literals();

    emit_render_into_end();
    emit_render_wrapper_start();
    emit_footer();
    return out_.str();
  }
};

// ============================================================
// CLI
// ============================================================

/**
 * @brief テンプレート→バイトコード変換用ダミーコンテキスト
 *
 * @details injamm の engine はテンプレートコンパイル時に glaze リフレクションで
 *          フィールドインデックスを解決する。コードジェネレータはフィールド名による
 *          直接アクセスコードを生成するため、インデックス解決結果は不要である。
 *          このダミー型は engine の型引数を満たすためだけに存在し、解決された
 *          フィールドインデックスはバイトコードシリアライズ時に破棄される。
 */
namespace {

struct DummyCtx {
  int _dummy{};
};

} // namespace

template <>
struct glz::meta<DummyCtx> {
  static constexpr auto value = glz::object("_dummy", &DummyCtx::_dummy);
};

/** @brief 使用方法を表示 */
void print_usage() {
  std::cerr << "用法: injamm_codegen (-i|--input <input.bc> | -e|--expr <template>) "
               "(-t|--type <Type>) (-o|--output <output.hpp>) [-n|--namespace <namespace>] "
               "[-p|--prefix <prefix>] [--no-simd]\n";
  std::cerr << "\nオプション:\n";
  std::cerr << "  -i, --input <file>     入力バイトコードファイル (.bc)\n";
  std::cerr << "  -e, --expr <string>    インラインテンプレート文字列\n";
  std::cerr << "  -t, --type <type>      データ型名 (例: UserData, myapp::UserInfo)\n";
  std::cerr << "  -o, --output <file>    出力ヘッダファイル (.hpp)\n";
  std::cerr << "  -n, --namespace <ns>   生成コードの名前空間 (デフォルト: generated)\n";
  std::cerr << "  -p, --prefix <prefix>  関数名プレフィックス (デフォルト: render)\n";
  std::cerr << "  --no-simd   SIMD命令を生成しない（デフォルト: 有効）\n";
  std::cerr << "  -h, --help  ヘルプ表示\n";
}

/**
 * @brief メインエントリポイント
 *
 * @details コマンド引数を解析し、テンプレート文字列またはバイトコードファイルから
 *          C++ レンダリング関数を生成する。-e が指定された場合は engine でテンプレートを
 *          コンパイル後、save_bytecode でバイナリシリアライズして既存の reader に
 *          受け渡す。-i が指定された場合は従来通り .bc ファイルを直接読み込む。
 *
 * @param argc 引数の個数
 * @param argv 引数の配列
 * @return int 正常終了時 0、エラー時 1
 */
int main(int argc, char* argv[]) {
  std::string input_path;   /**< .bc ファイルのパス（-i で指定） */
  std::string expr_tmpl;    /**< インラインテンプレート文字列（-e で指定） */
  std::string type_name;    /**< データ型名（-t で指定、例: UserData） */
  std::string output_path;  /**< 出力ヘッダファイルのパス（-o で指定） */
  std::string ns = "generated";   /**< 生成コードの名前空間（-n で指定、デフォルト: generated） */
  std::string prefix;       /**< 関数名プレフィックス（-p で指定、デフォルト: render） */
  bool no_simd = false;     /**< SIMD 最適化を無効化するフラグ（--no-simd で指定） */

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if ((arg == "-i" || arg == "--input") && i + 1 < argc) { input_path = argv[++i]; }
    else if ((arg == "-e" || arg == "--expr" || arg == "--template") && i + 1 < argc) { expr_tmpl = argv[++i]; }
    else if ((arg == "-t" || arg == "--type") && i + 1 < argc) { type_name = argv[++i]; }
    else if ((arg == "-o" || arg == "--output") && i + 1 < argc) { output_path = argv[++i]; }
    else if ((arg == "-n" || arg == "--namespace") && i + 1 < argc) { ns = argv[++i]; }
    else if ((arg == "-p" || arg == "--prefix") && i + 1 < argc) { prefix = argv[++i]; }
    else if (arg == "--no-simd") { no_simd = true; }
    else if (arg == "-h" || arg == "--help") { print_usage(); return 0; }
    else { std::cerr << "不明なオプション: " << arg << "\n"; print_usage(); return 1; }
  }

  /* -e か -i のいずれか、および -t, -o は必須 */
  if ((input_path.empty() && expr_tmpl.empty()) || type_name.empty() || output_path.empty()) {
    std::cerr << "エラー: -e (または -i) と -t, -o は必須です\n";
    print_usage();
    return 1;
  }

  /** @brief バイナリバイトコードデータのバッファ（-e のシリアライズ結果または .bc ファイルの内容） */
  std::string binary_data;

  if (!expr_tmpl.empty()) {
    /* ---- パス A: テンプレート文字列から直接生成 ---- */
    /* engine でテンプレートをコンパイルし、バイトコードをバイナリ形式にシリアライズする。
       シリアライズ後は既存の reader で読み込むことで、code_generator とのインタフェースを統一する。 */
    injamm::engine<DummyCtx> eng(expr_tmpl);
    auto const& real_bc = eng.get_bytecode();
    if (real_bc.error.has_error()) {
      std::cerr << "エラー: " << real_bc.error.message() << "\n";
      return 1;
    }
    std::ostringstream ss;
    auto ec = injamm::save_bytecode(real_bc, ss);
    if (ec != injamm::error_code::none) {
      std::cerr << "エラー: バイトコードのシリアライズに失敗しました\n";
      return 1;
    }
    binary_data = ss.str();
  } else {
    /* ---- パス B: .bc ファイルから読み込み（従来のパス） ---- */
    std::ifstream file(input_path, std::ios::binary | std::ios::ate);
    if (!file) {
      std::cerr << "エラー: " << input_path << " を開けません\n";
      return 1;
    }
    auto size = file.tellg();
    file.seekg(0);
    binary_data.resize(static_cast<std::size_t>(size));
    file.read(binary_data.data(), size);
  }

  /* バイトコードとして解析 */
  reader r(binary_data.data(), binary_data.size());
  auto bc = r.read_bytecode();
  if (!bc) {
    std::cerr << "エラー: バイトコードの解析に失敗しました\n";
    return 1;
  }

  /* C++ コード生成 */
  code_generator gen(type_name, ns, prefix, no_simd);
  auto code = gen.generate(*bc);
  if (!code) {
    std::cerr << "エラー: このテンプレートはコード生成に対応していない機能を含みます: "
              << gen.unsupported_feature() << "\n";
    return 1;
  }

  /* 生成されたコードをファイルに書き出し */
  std::ofstream out(output_path);
  if (!out) {
    std::cerr << "エラー: " << output_path << " を開けません\n";
    return 1;
  }
  out << *code;

  std::cout << "Generated: " << output_path << "\n";
  return 0;
}
