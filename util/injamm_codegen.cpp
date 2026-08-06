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
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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
  std::uint8_t compare_rhs_kind = 0;      /**< 比較の右オペランド種別 */
  std::string compare_rhs_text;           /**< 右オペランド文字列 */
  bool compare_rhs_has_dot = false;       /**< 右オペランドがドット区切りパスか */
  std::uint8_t filter_flags = 0;          /**< フィルタ特殊フラグ */
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

public:
  /**
   * @brief コンストラクタ
   * @param data 入力バッファ
   * @param size バッファサイズ（バイト）
   */
  reader(const char* data, std::size_t size) : data_(data), size_(size) {}

  /** @brief 読み込み状態が正常か */
  bool ok() const { return pos_ <= size_; }
  /** @brief 現在の読み込み位置 */
  std::size_t pos() const { return pos_; }

  /** @brief 1 バイト読み込み */
  std::uint8_t read_u8() {
    if (pos_ + 1 > size_) return 0;
    return static_cast<std::uint8_t>(data_[pos_++]);
  }

  /** @brief 32 ビット符号なし整数をリトルエンディアンで読み込み */
  std::uint32_t read_u32_le() {
    if (pos_ + 4 > size_) return 0;
    std::uint32_t v = 0;
    std::memcpy(&v, data_ + pos_, 4);
    pos_ += 4;
    return v;
  }

  /** @brief 64 ビット符号なし整数をリトルエンディアンで読み込み */
  std::uint64_t read_u64_le() {
    if (pos_ + 8 > size_) return 0;
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
    if (pos_ + len > size_) return {};
    std::string s(data_ + pos_, len);
    pos_ += len;
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

    auto fc = read_u64_le();
    ref.filters.reserve(fc);
    for (std::uint64_t i = 0; i < fc; ++i)
      ref.filters.push_back(read_string_filter_entry(literals));

    auto ifc = read_u64_le();
    ref.int_filters.reserve(ifc);
    for (std::uint64_t i = 0; i < ifc; ++i)
      ref.int_filters.push_back(read_int_filter_entry());

    auto ffc = read_u64_le();
    ref.float_filters.reserve(ffc);
    for (std::uint64_t i = 0; i < ffc; ++i)
      ref.float_filters.push_back(read_float_filter_entry());

    return ref;
  }

  /**
   * @brief バイトコード本体を読み込み
   * @details 命令列・リテラルテーブル・変数参照テーブル・partial を再帰的に読み込む。
   */
  bc::bytecode read_bytecode_body() {
    bc::bytecode bc;
    bc.is_simple = read_u8() != 0;
    bc.literal_total_size = read_u64_le();

    auto ic = read_u64_le();
    bc.instructions.reserve(ic);
    for (std::uint64_t i = 0; i < ic; ++i)
      bc.instructions.push_back(read_instruction());

    auto lc = read_u64_le();
    bc.literals.reserve(lc);
    for (std::uint64_t i = 0; i < lc; ++i)
      bc.literals.push_back(read_string());

    auto vc = read_u64_le();
    bc.var_refs.reserve(vc);
    for (std::uint64_t i = 0; i < vc; ++i)
      bc.var_refs.push_back(read_var_ref(bc.literals));

    auto pc = read_u64_le();
    bc.partial_entries.reserve(pc);
    for (std::uint64_t i = 0; i < pc; ++i) {
      auto name = read_string();
      auto local = read_u8() != 0;
      auto partial_bc = new bc::bytecode(read_bytecode_body());
      bc.partial_entries.push_back({std::move(name), local, partial_bc});
    }

    return bc;
  }

  /**
   * @brief バイトコードを読み込み
   * @details マジック "IJBC" + バージョン 1 のヘッダを検証し、
   *          バイトコード本体を読み込む。
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
    if (version != 1) return std::nullopt;

    return read_bytecode_body();
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
  int loop_depth_ = 0;         /**< 現在のループ深度 */
  int cond_section_depth_ = 0; /**< emit_at_section/inverted のネスト深度 */
  /**< インデックス形式（_iN/_sizeN）が必要な emit_section 命令 */
  std::unordered_set<const bc::instruction*> index_loops_;
  std::ostringstream out_;     /**< 出力ストリーム */

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
  std::string cpp_string(std::string_view s) {
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
   * @brief 変数参照を C++ のフィールドアクセス式に変換
   * @details ループ内では _item{depth}.field、ルートでは data.field を返す。
   *          ネストパス（ドット区切り）は data.path.to.field に展開する。
   */
  std::string resolve_access(bc::var_ref const& ref) {
    if (ref.is_loop_parent) {
      if (ref.has_dot) {
        auto pos = ref.key.find("loop.parent.");
        auto path = (pos != std::string::npos) ? ref.key.substr(pos + 12) : ref.key;
        return "data." + path;
      }
      return "data";
    }

    if (loop_depth_ > 0 && !ref.has_dot) {
      return "_item" + std::to_string(loop_depth_) + "." + ref.key;
    }

    if (ref.has_dot) {
      return "data." + ref.key;
    }

    return "data." + ref.key;
  }

  /**
   * @brief if 条件式を C++ の真偽式に変換
   * @details loop.is_last/loop.is_first は現在のループのインデックス比較に変換する。
   *          （{{#if loop.is_last}} 等。emit_at_section 相当の最適化を if 構文にも適用）
   */
  std::string resolve_if_access(bc::var_ref const& ref) {
    if (loop_depth_ > 0) {
      auto d = std::to_string(loop_depth_);
      if (ref.key == "loop.is_last") {
        return "_i" + d + " + 1 == _size" + d;
      }
      if (ref.key == "loop.is_first") {
        return "_i" + d + " == 0";
      }
    }
    return resolve_access(ref);
  }

  /**
   * @brief 比較値を取得
   * @details 整数比較の右辺値は var_ref.int_filters[0].arg に格納されている
   */
  std::string get_compare_value(bc::var_ref const& ref) {
    if (!ref.int_filters.empty())
      return std::to_string(ref.int_filters[0].arg);
    return "0";
  }

  /** @brief ヘッダファイルの先頭（インクルードガード + include）を生成 */
  void emit_header() {
    auto guard = func_prefix_.empty() ? "RENDER_HPP" : "RENDER_" + func_prefix_ + "_HPP";
    for (auto& c : guard) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
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
    emit_raw("");
    emit_raw("namespace generated {");
    emit_raw("#include \"codegen_helpers.hpp\"");
    emit_raw("");
  }

  /** @brief ヘルパ関数の生成（共通ヘッダを使用するため不要） */
  void emit_escape_func() {}
  void emit_number_conv() {}
  void emit_filter_helpers() {}

  /**
   * @brief 出力先バッファ版レンダリング関数の先頭を生成
   * @details render(data, out) -> expected<void> のシグニチャと本体冒頭。
   *          既存の std::string を出力先として受け取りバッファを再利用する。
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
    emit_raw(" *          出力先バッファを引数で受け取り、内部バッファを再利用することで");
    emit_raw(" *          アロケーションを削減する。");
    emit_raw(" *");
    emit_raw(" * @tparam T データ型（フィールドへのアクセスが必要）");
    emit_raw(" * @param data レンダリング対象のデータ");
    emit_raw(" * @param out  出力先バッファ（内容はクリアされる）");
    emit_raw(" * @return 正常時: void。エラー時: error_ctx");
    emit_raw(" */");
    emit_raw("template <typename T>");
    emit_raw("[[nodiscard]] std::expected<void, injamm::error_ctx>");
    emit_raw(func_name + "(const T& data, std::string& out) {");
    ++indent_;
    emit("out.clear();");
    emit("out.reserve(" + std::to_string(reserve_size) + ");");
    if (declare_filtered) {
      // フィルタバッファは関数先頭で宣言し assign で再利用する
      // （ループ内フィルタで毎回アロケーションしないための工夫）
      emit("std::string _filtered;");
      emit("_filtered.reserve(64);");
    }
    emit("");
  }

  /** @brief 出力先バッファ版レンダリング関数の末尾を生成 */
  void emit_render_into_end() {
    emit("");
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
    /* 非ローカル partial でかつ空でないバイトコードを持つものの数を数える */
    std::size_t partial_count = 0;
    for (auto const& pe : bc.partial_entries) {
      if (!pe.local && pe.bc && !pe.bc->instructions.empty())
        ++partial_count;
    }
    /* partial が1つもなければ dispatch 関数を出力しない */
    if (partial_count == 0) return;

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
    emit_raw("template <injamm::fixed_string PartialName, typename T>");
    emit_raw("[[nodiscard]] std::expected<void, injamm::error_ctx>");
    emit_raw("render_partial(const T& data, std::string& out) {");
    ++indent_;

    /* if constexpr チェーン: 各 partial 名を std::string_view で比較する。
       コンパイル時解決なので実行時オーバーヘッドは一切ない。 */
    bool first = true;
    for (auto const& pe : bc.partial_entries) {
      if (pe.local || !pe.bc || pe.bc->instructions.empty()) continue;

      auto name_lit = cpp_string(pe.name);
      if (first) {
        emit("if constexpr (std::string_view(PartialName.data) == " + name_lit + ") {");
        first = false;
      } else {
        emit("else if constexpr (std::string_view(PartialName.data) == " + name_lit + ") {");
      }
      ++indent_;

      /* loop_depth_ などの状態を退避: 各 partial は独立した関数内部と同じ扱い */
      auto saved_loop = loop_depth_;
      auto saved_cond = cond_section_depth_;
      loop_depth_ = 0;
      cond_section_depth_ = 0;

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

      loop_depth_ = saved_loop;
      cond_section_depth_ = saved_cond;

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
    emit_raw("");
  }

  /** @brief フッタ（名前空間クローズ + インクルードガード終了）を生成 */
  void emit_footer() {
    emit_raw("} // namespace generated");
    auto guard = func_prefix_.empty() ? "RENDER_HPP" : "RENDER_" + func_prefix_ + "_HPP";
    for (auto& c : guard) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    emit_raw("");
    emit_raw("#endif // " + guard);
  }

  /**
   * @brief ループ本体がインデックス変数 (_iN/_sizeN) を参照する emit_section を事前収集
   * @details @index/@first/@last/@size および loop.is_first/is_last 系
   *          (emit_at_section/emit_at_inverted) を本体に含むループはインデックス形式を
   *          維持し、それ以外は range-for に変換する。ネストループはスタックで追跡し、
   *          インデックス参照命令は最も内側のループに帰属させる。
   * @param bc 解析対象のバイトコード
   */
  void precompute_index_loops(bc::bytecode const& bc) {
    index_loops_.clear();
    std::vector<const bc::instruction*> stack;
    for (auto const& inst : bc.instructions) {
      auto op = inst.op;
      if (op == bc::opcode::emit_section) {
        stack.push_back(&inst);
      }
      else if (op == bc::opcode::emit_at_index || op == bc::opcode::emit_at_index1 ||
               op == bc::opcode::emit_at_first || op == bc::opcode::emit_at_last ||
               op == bc::opcode::emit_at_size || op == bc::opcode::emit_at_section ||
               op == bc::opcode::emit_at_inverted) {
        if (!stack.empty()) index_loops_.insert(stack.back());
      }
      else if (op == bc::opcode::emit_if || op == bc::opcode::emit_if_not ||
               op == bc::opcode::emit_if_or || op == bc::opcode::emit_if_and) {
        auto const& ref = bc.var_refs[inst.operand2];
        if (!stack.empty() && (ref.key == "loop.is_last" || ref.key == "loop.is_first")) {
          index_loops_.insert(stack.back());
        }
      }
      else if (op == bc::opcode::emit_end) {
        if (!stack.empty()) stack.pop_back();
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
   * @brief 個々のバイトコード命令を C++ コードに変換
   * @param inst 変換する命令
   * @param bc 含まれるバイトコード（リテラル・変数参照テーブルへのアクセス用）
   */
  void emit_instruction(bc::instruction const& inst, bc::bytecode const& bc) {
    auto op = inst.op;

    if (op == bc::opcode::emit_literal) {
      emit("out += " + cpp_string(bc.literals[inst.operand]) + ";");
    }
    else if (op == bc::opcode::emit_var) {
      auto access = resolve_access(bc.var_refs[inst.operand]);
      emit("html_escape_append_value(out, " + access + ");");
    }
    else if (op == bc::opcode::emit_var_raw) {
      auto access = resolve_access(bc.var_refs[inst.operand]);
      emit("append_value(out, " + access + ");");
    }
    else if (op == bc::opcode::emit_section) {
      auto access = resolve_access(bc.var_refs[inst.operand2]);
      ++loop_depth_;
      auto idx = std::to_string(loop_depth_);
      if (index_loops_.count(&inst) != 0) {
        /* @index/@first/@last/@size 等を参照するループはインデックス形式を維持 */
        emit("auto _size" + idx + " = " + access + ".size();");
        emit("for (std::size_t _i" + idx + " = 0; _i" + idx + " < _size" + idx + "; ++_i" + idx + ") {");
        ++indent_;
        emit("const auto& _item" + idx + " = " + access + "[_i" + idx + "];");
      } else {
        /* インデックス変数を参照しないループは range-for に変換（境界チェック削減） */
        emit("for (const auto& _item" + idx + " : " + access + ") {");
        ++indent_;
      }
    }
    else if (op == bc::opcode::emit_at_section) {
      auto kind = inst.operand2;
      auto d = std::to_string(loop_depth_);
      if (kind == 0) {
        emit("if (_i" + d + " > 0) {");
      } else if (kind == 1) {
        emit("if (_i" + d + " == 0) {");
      } else if (kind == 2) {
        emit("if (_i" + d + " + 1 == _size" + d + ") {");
      }
      ++indent_;
      ++cond_section_depth_;
    }
    else if (op == bc::opcode::emit_at_inverted) {
      auto kind = inst.operand2;
      auto d = std::to_string(loop_depth_);
      if (kind == 0) {
        emit("if (_i" + d + " == 0) {");
      } else if (kind == 1) {
        emit("if (_i" + d + " > 0) {");
      } else if (kind == 2) {
        emit("if (_i" + d + " + 1 < _size" + d + ") {");
      }
      ++indent_;
      ++cond_section_depth_;
    }
    else if (op == bc::opcode::emit_end) {
      if (cond_section_depth_ > 0) {
        --indent_;
        emit("}");
        --cond_section_depth_;
      } else if (loop_depth_ > 0) {
        --indent_;
        emit("}");
        --loop_depth_;
      }
    }
    else if (op == bc::opcode::emit_inverted) {
      auto access = resolve_access(bc.var_refs[inst.operand2]);
      emit("if (value_empty(" + access + ")) {");
      ++indent_;
    }
    else if (op == bc::opcode::emit_at_index) {
      emit("append_number(out, _i" + std::to_string(loop_depth_) + ");");
    }
    else if (op == bc::opcode::emit_at_index1) {
      emit("append_number(out, _i" + std::to_string(loop_depth_) + " + 1);");
    }
    else if (op == bc::opcode::emit_at_first) {
      emit("out += (_i" + std::to_string(loop_depth_) + " == 0) ? \"true\" : \"false\";");
    }
    else if (op == bc::opcode::emit_at_last) {
      auto d = std::to_string(loop_depth_);
      emit("out += (_i" + d + " == _size" + d + " - 1) ? \"true\" : \"false\";");
    }
    else if (op == bc::opcode::emit_at_size) {
      emit("append_number(out, _size" + std::to_string(loop_depth_) + ");");
    }
    else if (op == bc::opcode::emit_if) {
      auto access = resolve_if_access(bc.var_refs[inst.operand2]);
      emit("if (static_cast<bool>(" + access + ")) {");
      ++indent_;
    }
    else if (op == bc::opcode::emit_if_eq || op == bc::opcode::emit_if_ne ||
             op == bc::opcode::emit_if_gt || op == bc::opcode::emit_if_gte ||
             op == bc::opcode::emit_if_lt || op == bc::opcode::emit_if_lte) {
      auto access = resolve_access(bc.var_refs[inst.operand2]);
      auto cmp_val = get_compare_value(bc.var_refs[inst.operand2]);
      std::string op_str;
      switch (op) {
        case bc::opcode::emit_if_eq:  op_str = "=="; break;
        case bc::opcode::emit_if_ne:  op_str = "!="; break;
        case bc::opcode::emit_if_gt:  op_str = ">";  break;
        case bc::opcode::emit_if_gte: op_str = ">="; break;
        case bc::opcode::emit_if_lt:  op_str = "<";  break;
        case bc::opcode::emit_if_lte: op_str = "<="; break;
        default: break;
      }
      emit("if (" + access + " " + op_str + " " + cmp_val + ") {");
      ++indent_;
    }
    else if (op == bc::opcode::emit_else) {
      --indent_;
      emit("} else {");
      ++indent_;
    }
    else if (op == bc::opcode::emit_endif) {
      --indent_;
      emit("}");
    }
    else if (op == bc::opcode::emit_litvar) {
      emit("out += " + cpp_string(bc.literals[inst.operand]) + ";");
      emit("html_escape_append_value(out, " + resolve_access(bc.var_refs[inst.operand2]) + ");");
    }
    else if (op == bc::opcode::emit_litvar_raw) {
      emit("out += " + cpp_string(bc.literals[inst.operand]) + ";");
      emit("out += " + resolve_access(bc.var_refs[inst.operand2]) + ";");
    }
    else if (op == bc::opcode::emit_at_root) {
      emit("html_escape_append_value(out, data);");
    }
    else if (op == bc::opcode::emit_at_root_field) {
      emit("html_escape_append_value(out, data." + bc.var_refs[inst.operand].key + ");");
    }
    else if (op == bc::opcode::emit_at_root_field_raw) {
      emit("out += data." + bc.var_refs[inst.operand].key + ";");
    }
    else if (op == bc::opcode::emit_at_key) {
      emit("out += _key" + std::to_string(loop_depth_) + ";");
    }
    else if (op == bc::opcode::emit_this) {
      if (loop_depth_ > 0) {
        emit("html_escape_append_value(out, _item" + std::to_string(loop_depth_) + ");");
      } else {
        emit("html_escape_append_value(out, data);");
      }
    }
    else if (op == bc::opcode::emit_filtered) {
      emit("html_escape_append(out, _filtered);");
    }
    else if (op == bc::opcode::emit_filtered_raw) {
      emit("out += _filtered;");
    }
    else if (op == bc::opcode::resolve_filtered) {
      auto const& ref = bc.var_refs[inst.operand2];
      auto access = resolve_access(ref);
      bool use_json = (ref.filter_flags & 1) != 0;
      if (use_json) {
        // runtime: reflectable → glz::write_json, serializable → serialize_value (raw)
        emit("if constexpr (::glz::reflectable<decltype(" + access + ")>) {");
        ++indent_;
        emit("(void)::glz::write_json(" + access + ", _filtered);");
        --indent_;
        emit("} else {");
        ++indent_;
        emit("_filtered.assign(" + access + ");");
        --indent_;
        emit("}");
      } else {
        // _filtered は関数先頭で宣言済み: assign でバッファ容量を再利用する
        emit("_filtered.assign(" + access + ");");
      }
    }
    else if (op == bc::opcode::filter_string) {
      // 汎用文字列フィルタ: operand2 = string_filter 種別, operand = arg1, operand3 = 文字列引数
      auto const kind = static_cast<injamm::detail::string_filter>(inst.operand2);
      auto const arg = std::to_string(inst.operand);
      std::string lit1, lit2;
      if (inst.operand3 != UINT32_MAX) {
        if (inst.operand3 < bc.literals.size()) lit1 = cpp_string(bc.literals[inst.operand3]);
        if (kind == injamm::detail::string_filter::replace && inst.operand3 + 1 < bc.literals.size()) lit2 = cpp_string(bc.literals[inst.operand3 + 1]);
      }
      switch (kind) {
      case injamm::detail::string_filter::upper:      emit("filter_to_upper(_filtered);"); break;
      case injamm::detail::string_filter::lower:      emit("filter_to_lower(_filtered);"); break;
      case injamm::detail::string_filter::capitalize: emit("filter_capitalize(_filtered);"); break;
      case injamm::detail::string_filter::title:      emit("filter_title(_filtered);"); break;
      case injamm::detail::string_filter::trim:       emit("filter_trim(_filtered);"); break;
      case injamm::detail::string_filter::ltrim:      emit("filter_ltrim(_filtered);"); break;
      case injamm::detail::string_filter::rtrim:      emit("filter_rtrim(_filtered);"); break;
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
      if (kind == injamm::detail::float_filter::precision)
        emit("filter_float_precision(_filtered, " + std::to_string(inst.operand) + ");");
    }
    else if (op == bc::opcode::emit_if_filtered) {
      bool invert = inst.operand != 0;
      if (invert) {
        emit("if (!static_cast<bool>(_filtered.empty())) {");
      } else {
        emit("if (!_filtered.empty()) {");
      }
      ++indent_;
    }
    else if (op == bc::opcode::emit_if_or) {
      auto access = resolve_if_access(bc.var_refs[inst.operand2]);
      emit("if (static_cast<bool>(" + access + ")) {");
      ++indent_;
    }
    else if (op == bc::opcode::emit_if_and) {
      auto access = resolve_if_access(bc.var_refs[inst.operand2]);
      emit("if (static_cast<bool>(" + access + ")) {");
      ++indent_;
    }
    else if (op == bc::opcode::emit_if_not) {
      auto access = resolve_if_access(bc.var_refs[inst.operand2]);
      emit("if (!static_cast<bool>(" + access + ")) {");
      ++indent_;
    }
    else if (op == bc::opcode::call_partial) {
      /* partial 呼び出し: 従来はインライン展開していたが、emit_partial_dispatch で
         生成された render_partial<"name"> 関数を呼び出すことで外部からも個別に
         呼び出せるようになった。(void) キャストは nodiscard 警告の抑制。 */
      auto& pe = bc.partial_entries[inst.operand];
      if (!pe.local) {
        emit("(void)render_partial<\"" + pe.name + "\">(data, out);");
      }
    }
    else if (op == bc::opcode::emit_var_size) {
      emit("append_number(out, value_size(" + resolve_access(bc.var_refs[inst.operand2]) + "));");
    }
    else if (op == bc::opcode::emit_break) {
      emit("break;");
    }
    else if (op == bc::opcode::emit_continue) {
      emit("continue;");
    }
    else if (op == bc::opcode::halt) {
      // no-op
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

  /**
   * @brief バイトコードから C++ コードを生成
   * @param bc 入力バイトコード
   * @return 生成された C++ ヘッダファイルの内容
   */
  std::string generate(bc::bytecode const& bc) {
    // リテラルの合計サイズを計算して reserve に使用
    std::size_t total_literal_size = 0;
    for (auto const& lit : bc.literals) {
      total_literal_size += lit.size();
    }

    emit_header();
    /* partial ディスパッチ関数を先に出力: メイン関数から render_partial<"name"> として
       呼び出せるようにする。partial がない場合は何も出力しない。 */
    emit_partial_dispatch(bc);
    emit_render_into_start(total_literal_size, uses_filtered(bc));

    /* range-for 化の判定をループ命令の生成前に済ませる */
    precompute_index_loops(bc);

    // 隣接リテラルを結合して出力
    std::string accumulated_literals;
    auto flush_literals = [&]() {
      if (!accumulated_literals.empty()) {
        emit("out += " + cpp_string(accumulated_literals) + ";");
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
        emit("html_escape_append_value(out, " + resolve_access(bc.var_refs[inst.operand2]) + ");");
        continue;
      }
      if (op == bc::opcode::emit_litvar_raw) {
        accumulated_literals += bc.literals[inst.operand];
        flush_literals();
        emit("out += " + resolve_access(bc.var_refs[inst.operand2]) + ";");
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
  std::cerr << "用法: injamm_codegen (-i <input.bc> | -e <template>) -t <Type> -o <output.hpp> [-n <namespace>] [-p <prefix>] [--no-simd]\n";
  std::cerr << "\nオプション:\n";
  std::cerr << "  -i <file>   入力バイトコードファイル (.bc)\n";
  std::cerr << "  -e <string> インラインテンプレート文字列\n";
  std::cerr << "  -t <type>   データ型名 (例: UserData, myapp::UserInfo)\n";
  std::cerr << "  -o <file>   出力ヘッダファイル (.hpp)\n";
  std::cerr << "  -n <ns>     生成コードの名前空間 (デフォルト: generated)\n";
  std::cerr << "  -p <prefix> 関数名プレフィックス (デフォルト: render)\n";
  std::cerr << "  --no-simd   SIMD命令を生成しない（デフォルト: 有効）\n";
  std::cerr << "  -h          ヘルプ表示\n";
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
    if (arg == "-i" && i + 1 < argc) { input_path = argv[++i]; }
    else if ((arg == "-e" || arg == "--template") && i + 1 < argc) { expr_tmpl = argv[++i]; }
    else if (arg == "-t" && i + 1 < argc) { type_name = argv[++i]; }
    else if (arg == "-o" && i + 1 < argc) { output_path = argv[++i]; }
    else if (arg == "-n" && i + 1 < argc) { ns = argv[++i]; }
    else if (arg == "-p" && i + 1 < argc) { prefix = argv[++i]; }
    else if (arg == "--no-simd") { no_simd = true; }
    else if (arg == "-h") { print_usage(); return 0; }
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

  /* 生成されたコードをファイルに書き出し */
  std::ofstream out(output_path);
  if (!out) {
    std::cerr << "エラー: " << output_path << " を開けません\n";
    return 1;
  }
  out << code;

  std::cout << "Generated: " << output_path << "\n";
  return 0;
}
