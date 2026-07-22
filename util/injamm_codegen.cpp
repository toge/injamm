/**
 * @file injamm_codegen.cpp
 * @brief バイトコード(.bc)から glaze 非依存の C++ レンダリング関数を生成する
 *
 * @details このツールは injamm_bc で生成されたバイトコードを読み込み、
 *          glaze に依存しない C++ テンプレート関数を生成する。
 *          生成された関数は直接フィールドアクセスにより高速にレンダリングを行う。
 *
 *          用法: injamm_codegen -i template.bc -t UserData -o render.hpp
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
#include <vector>

// ============================================================
// バイトコード定義（glaze 非依存の最小限の型）
// ============================================================

namespace bc {

/**
 * @brief バイトコードオペコード
 * @details injamm のバイトコード VM が解釈する命令の種類。
 *          injamm::detail::bc_opcode と同一だが glaze に依存しない独立定義。
 */
enum class opcode : std::uint8_t {
  emit_literal = 0,       /**< リテラル文字列出力 */
  emit_var = 1,           /**< 変数参照（HTML エスケープあり） */
  emit_var_raw = 2,       /**< 変数参照（生出力、エスケープなし） */
  emit_section = 3,       /**< セクション開始（配列ループ） */
  emit_end = 4,           /**< セクション終了 */
  emit_inverted = 5,      /**< 反転セクション開始 ({^{section}}) */
  emit_at_index = 6,      /**< @index 出力（ループ内の現在インデックス） */
  emit_at_first = 7,      /**< @first 出力（ループの先頭要素か） */
  emit_at_last = 8,       /**< @last 出力（ループの末尾要素か） */
  emit_if = 9,            /**< if 条件分岐 */
  emit_if_eq = 10,        /**< if (var == int_literal) 分岐 */
  emit_if_ne = 11,        /**< if (var != int_literal) 分岐 */
  emit_if_gt = 12,        /**< if (var > int_literal) 分岐 */
  emit_if_gte = 13,       /**< if (var >= int_literal) 分岐 */
  emit_if_lt = 14,        /**< if (var < int_literal) 分岐 */
  emit_if_lte = 15,       /**< if (var <= int_literal) 分岐 */
  emit_else = 16,         /**< else ジャンプ先 */
  emit_endif = 17,        /**< endif（if ブロック終了） */
  emit_at_section = 18,   /**< @first/@last/@index によるセクション制御 */
  emit_at_inverted = 19,  /**< ^@first/^@last/^@index 反転セクション */
  emit_litvar = 20,       /**< 融合命令: リテラル + 変数（エスケープあり） */
  emit_litvar_raw = 21,   /**< 融合命令: リテラル + 変数（生出力） */
  emit_at_root = 22,      /**< @root 出力（ルートコンテキストのシリアライズ） */
  emit_at_root_field = 23,    /**< @root.field ルートフィールド解決（エスケープあり） */
  emit_at_root_field_raw = 24, /**< @root.field ルートフィールド解決（生出力） */
  emit_at_key = 25,       /**< @key 出力（ループ内の現在要素キー名） */
  emit_this = 26,         /**< 現在のコンテキスト自体のシリアライズ */
  resolve_filtered = 27,  /**< フィルタ付き変数解決: 値を一時バッファに解決 */
  filter_upper = 28,      /**< ASCII 大文字変換 */
  filter_lower = 29,      /**< ASCII 小文字変換 */
  filter_capitalize = 30, /**< 先頭の文字を大文字にする */
  filter_title = 31,      /**< 単語の先頭を大文字にする */
  filter_trim = 32,       /**< 先頭末尾の空白除去 */
  filter_ltrim = 33,      /**< 先頭の空白除去 */
  filter_rtrim = 34,      /**< 末尾の空白除去 */
  filter_left = 35,       /**< 左寄せ（引数: 幅） */
  filter_right = 36,      /**< 右寄せ（引数: 幅） */
  filter_center = 37,     /**< 中央寄せ（引数: 幅） */
  filter_truncate = 38,   /**< 文字列切り詰め（引数: 最大文字数） */
  filter_substr = 39,     /**< 部分文字列（引数1: 開始位置, 引数2: 文字数） */
  filter_replace = 40,    /**< 部分文字列置換 */
  filter_default = 41,    /**< デフォルト値フィルタ（引数: literal index） */
  filter_json = 42,       /**< JSON 出力フィルタ */
  filter_safe = 43,       /**< 生出力マーク（エスケープ抑制） */
  filter_indent = 44,     /**< インデント（引数: 空白数） */
  filter_pad = 45,        /**< パディング（引数1: 幅, 引数2: 埋め文字 literal index） */
  filter_pluralize = 46,  /**< 単数形/複数形（引数1: 単数 literal index, 引数2: 複数 literal index） */
  filter_format = 47,     /**< strftime 形式 chrono フォーマット */
  filter_repeat = 48,     /**< 文字列繰り返し（引数: 回数） */
  emit_filtered = 49,     /**< フィルタ後の文字列出力（エスケープあり） */
  emit_filtered_raw = 50, /**< フィルタ後の文字列出力（生出力） */
  filter_int_abs = 51,    /**< 整数絶対値変換 */
  filter_int_hex = 52,    /**< 整数 16 進数変換 */
  filter_int_oct = 53,    /**< 整数 8 進数変換 */
  filter_int_bin = 54,    /**< 整数 2 進数変換 */
  filter_int_neg = 55,    /**< 整数符号逆転 */
  filter_int_mod = 56,    /**< 整数余り（引数: 除数） */
  filter_int_numify = 57,     /**< 整数 3 桁カンマ区切り */
  filter_int_is_neg = 58,     /**< 負数判定: "true"/"false" を出力 */
  filter_int_eq = 59,         /**< 等価判定: 引数と比較し "true"/"false" を出力 */
  filter_int_ne = 60,         /**< 不等価判定 */
  filter_int_gt = 61,         /**< 大なり判定 */
  filter_int_gte = 62,        /**< 以上判定 */
  filter_int_lt = 63,         /**< 小なり判定 */
  filter_int_lte = 64,        /**< 以下判定 */
  filter_int_zerofill = 65,   /**< 整数 0 埋め（引数: 最小桁数） */
  filter_int_add = 66,        /**< 整数加算（引数: 加算値） */
  filter_int_sub = 67,        /**< 整数減算（引数: 減算値） */
  filter_int_mul = 68,        /**< 整数乗算（引数: 乗算値） */
  filter_int_div = 69,        /**< 整数除算（引数: 除算値） */
  filter_float_precision = 70, /**< 実数小数点以下桁数（引数: 桁数） */
  emit_if_filtered = 71,  /**< フィルタ適用済み値での if 分岐 */
  emit_break = 72,        /**< ループ脱出 */
  emit_continue = 73,     /**< 次のイテレーションへスキップ */
  emit_at_index1 = 74,    /**< ループ 1 始まりインデックス */
  emit_at_size = 75,      /**< ループ総要素数 */
  emit_var_size = 76,     /**< 変数の要素数 ({/{field.size}}) */
  emit_if_or = 77,        /**< if (a || b) 分岐 */
  emit_if_and = 78,       /**< if (a && b) 分岐 */
  emit_if_not = 79,       /**< if (!a) 分岐 */
  call_partial = 80,      /**< 名前付き partial 呼び出し */
  halt = 81,              /**< プログラム終了 */
};

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
  bool filtered_declared_ = false; /**< _filtered 変数が宣言済みか */
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
    if (no_simd_) {
      emit_raw("#define INJAMM_CODEGEN_DISABLE_SIMD 1");
    } else {
      emit_raw("#include <injamm/escape.hpp>");
    }
    emit_raw("#include \"codegen_helpers.hpp\"");
    emit_raw("");
    emit_raw("namespace generated {");
    emit_raw("");
  }

  /** @brief ヘルパ関数の生成（共通ヘッダを使用するため不要） */
  void emit_escape_func() {}
  void emit_number_conv() {}
  void emit_filter_helpers() {}

  /**
   * @brief レンダリング関数の先頭を生成
   * @details テンプレート関数のシグニチャと関数本体の冒頭を出力する。
   *          生成される関数は汎用テンプレートで、任意の型 T で利用可能。
   * @param reserve_size 文字列バッファの事前確保サイズ（バイト）
   */
  void emit_render_start(std::size_t reserve_size = 256) {
    emit_raw("");
    emit_raw("/**");
    emit_raw(" * @brief テンプレート文字列から生成されたレンダリング関数");
    emit_raw(" *");
    emit_raw(" * @details injamm_codegen によって自動生成された関数。");
    emit_raw(" *          テンプレート引数 T は data.name, data.age 等の");
    emit_raw(" *          フィールドにアクセス可能な型でなければならない。");
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
    auto func_name = func_prefix_.empty() ? "render" : func_prefix_;
    emit_raw(func_name + "(const T& data) {");
    ++indent_;
    emit("std::string out;");
    emit("out.reserve(" + std::to_string(reserve_size) + ");");
    emit("");
  }

  /** @brief レンダリング関数の末尾を生成 */
  void emit_render_end() {
    emit("");
    emit("return out;");
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
      auto access = resolve_access(bc.var_refs[inst.operand2]);
      emit("html_escape_append_value(out, " + access + ");");
    }
    else if (op == bc::opcode::emit_var_raw) {
      auto access = resolve_access(bc.var_refs[inst.operand2]);
      emit("append_value(out, " + access + ");");
    }
    else if (op == bc::opcode::emit_section) {
      auto access = resolve_access(bc.var_refs[inst.operand2]);
      ++loop_depth_;
      auto idx = std::to_string(loop_depth_);
      emit("for (std::size_t _i" + idx + " = 0; _i" + idx + " < " + access + ".size(); ++_i" + idx + ") {");
      ++indent_;
      emit("const auto& _item" + idx + " = " + access + "[_i" + idx + "];");
    }
    else if (op == bc::opcode::emit_end) {
      if (loop_depth_ > 0) {
        --indent_;
        emit("}");
        --loop_depth_;
      }
    }
    else if (op == bc::opcode::emit_inverted) {
      auto access = resolve_access(bc.var_refs[inst.operand2]);
      emit("if (" + access + ".empty()) {");
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
      emit("out += (_i" + d + " == _item" + d + ".size() - 1) ? \"true\" : \"false\";");
    }
    else if (op == bc::opcode::emit_at_size) {
      emit("append_number(out, _item" + std::to_string(loop_depth_) + ".size());");
    }
    else if (op == bc::opcode::emit_if) {
      auto access = resolve_access(bc.var_refs[inst.operand2]);
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
    else if (op == bc::opcode::emit_filtered) {
      emit("html_escape_append(out, _filtered);");
    }
    else if (op == bc::opcode::emit_filtered_raw) {
      emit("out += _filtered;");
    }
    else if (op == bc::opcode::resolve_filtered) {
      auto access = resolve_access(bc.var_refs[inst.operand2]);
      if (!filtered_declared_) {
        emit("std::string _filtered = " + access + ";");
        filtered_declared_ = true;
      } else {
        emit("_filtered = " + access + ";");
      }
    }
    else if (op == bc::opcode::filter_upper) {
      emit("filter_to_upper(_filtered);");
    }
    else if (op == bc::opcode::filter_lower) {
      emit("filter_to_lower(_filtered);");
    }
    else if (op == bc::opcode::filter_capitalize) {
      emit("filter_capitalize(_filtered);");
    }
    else if (op == bc::opcode::filter_trim) {
      emit("filter_trim(_filtered);");
    }
    else if (op == bc::opcode::filter_ltrim) {
      emit("filter_ltrim(_filtered);");
    }
    else if (op == bc::opcode::filter_rtrim) {
      emit("filter_rtrim(_filtered);");
    }
    else if (op == bc::opcode::filter_truncate) {
      emit("filter_truncate(_filtered, " + std::to_string(inst.operand) + ");");
    }
    else if (op == bc::opcode::filter_substr) {
      emit("filter_substr(_filtered, " + std::to_string(inst.operand) + ", " + std::to_string(inst.operand2) + ");");
    }
    else if (op == bc::opcode::filter_repeat) {
      emit("filter_repeat(_filtered, " + std::to_string(inst.operand) + ");");
    }
    else if (op == bc::opcode::filter_int_numify) {
      emit("filter_numify(_filtered);");
    }
    else if (op == bc::opcode::filter_int_zerofill) {
      emit("filter_zerofill(_filtered, " + std::to_string(inst.operand) + ");");
    }
    else if (op == bc::opcode::emit_var_size) {
      emit("append_number(out, " + resolve_access(bc.var_refs[inst.operand2]) + ".size());");
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
    filtered_declared_ = false;

    // リテラルの合計サイズを計算して reserve に使用
    std::size_t total_literal_size = 0;
    for (auto const& lit : bc.literals) {
      total_literal_size += lit.size();
    }

    emit_header();
    emit_render_start(total_literal_size);

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

    emit_render_end();
    emit_footer();
    return out_.str();
  }
};

// ============================================================
// CLI
// ============================================================

/** @brief 使用方法を表示 */
void print_usage() {
  std::cerr << "用法: injamm_codegen -i <input.bc> -t <Type> -o <output.hpp> [-n <namespace>] [-p <prefix>] [--no-simd]\n";
  std::cerr << "\nオプション:\n";
  std::cerr << "  -i <file>   入力バイトコードファイル (.bc)\n";
  std::cerr << "  -t <type>   データ型名 (例: UserData, myapp::UserInfo)\n";
  std::cerr << "  -o <file>   出力ヘッダファイル (.hpp)\n";
  std::cerr << "  -n <ns>     生成コードの名前空間 (デフォルト: generated)\n";
  std::cerr << "  -p <prefix> 関数名プレフィックス (デフォルト: render)\n";
  std::cerr << "  --no-simd   SIMD命令を生成しない（デフォルト: 有効）\n";
  std::cerr << "  -h          ヘルプ表示\n";
}

/**
 * @brief メインエントリポイント
 * @details コマンド引数を解析し、バイトコードを読み込んで C++ コードを生成する
 */
int main(int argc, char* argv[]) {
  std::string input_path;
  std::string type_name;
  std::string output_path;
  std::string ns = "generated";
  std::string prefix;
  bool no_simd = false;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "-i" && i + 1 < argc) { input_path = argv[++i]; }
    else if (arg == "-t" && i + 1 < argc) { type_name = argv[++i]; }
    else if (arg == "-o" && i + 1 < argc) { output_path = argv[++i]; }
    else if (arg == "-n" && i + 1 < argc) { ns = argv[++i]; }
    else if (arg == "-p" && i + 1 < argc) { prefix = argv[++i]; }
    else if (arg == "--no-simd") { no_simd = true; }
    else if (arg == "-h") { print_usage(); return 0; }
    else { std::cerr << "不明なオプション: " << arg << "\n"; print_usage(); return 1; }
  }

  if (input_path.empty() || type_name.empty() || output_path.empty()) {
    std::cerr << "エラー: -i, -t, -o は必須です\n";
    print_usage();
    return 1;
  }

  // ファイル読み込み
  std::ifstream file(input_path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "エラー: " << input_path << " を開けません\n";
    return 1;
  }

  auto size = file.tellg();
  file.seekg(0);
  std::string data(static_cast<std::size_t>(size), '\0');
  file.read(data.data(), size);

  // バイトコード解析
  reader r(data.data(), data.size());
  auto bc = r.read_bytecode();
  if (!bc) {
    std::cerr << "エラー: バイトコードの解析に失敗しました\n";
    return 1;
  }

  // C++ コード生成
  code_generator gen(type_name, ns, prefix, no_simd);
  auto code = gen.generate(*bc);

  // 出力
  std::ofstream out(output_path);
  if (!out) {
    std::cerr << "エラー: " << output_path << " を開けません\n";
    return 1;
  }
  out << code;

  std::cout << "Generated: " << output_path << "\n";
  return 0;
}
