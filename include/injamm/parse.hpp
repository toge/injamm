#pragma once
#include "detail/append_guard.hpp"

#include "bytecode_fwd.hpp"
#include "types.hpp"
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace injamm::detail {

/**
 * @brief 文字列から整数をパースする（負数対応）
 * @param s 入力文字列
 * @return パース結果（負の符号対応）
 * @note 数字のみを処理し、先頭の '-' を負号として解釈する。非数字は無視。
 *       オーバーフロー時は INT_MAX / INT_MIN にクランプする。
 */
[[nodiscard]] constexpr int parse_int_arg(std::string_view s) noexcept {
  long long  arg      = 0;
  bool negative = false;
  std::size_t i = 0;
  if (i < s.size() && s[i] == '-') {
    negative = true;
    ++i;
  }
  for (; i < s.size(); ++i) {
    if (s[i] >= '0' && s[i] <= '9') {
      arg = arg * 10 + (s[i] - '0');
      if (!negative && arg > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
      if (negative && -arg < std::numeric_limits<int>::min()) return std::numeric_limits<int>::min();
    }
  }
  return static_cast<int>(negative ? -arg : arg);
}

/**
 * @brief 厳密な整数リテラルをパースする
 * @param s 入力文字列
 * @return パース成功時は int、失敗時は std::nullopt
 * @details 先頭の `-` を1回だけ許容し、それ以外の文字が含まれる場合は失敗する。
 *          値域は int に収まることを要求する。
 */
[[nodiscard]] constexpr std::optional<int> parse_int_literal(std::string_view s) noexcept {
  if (s.empty()) {
    return std::nullopt;
  }

  bool negative = false;
  std::size_t i = 0;
  if (s[i] == '-') {
    negative = true;
    ++i;
    if (i == s.size()) {
      return std::nullopt;
    }
  }

  long long value = 0;
  for (; i < s.size(); ++i) {
    auto const c = s[i];
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + static_cast<long long>(c - '0');
    if (!negative && value > std::numeric_limits<int>::max()) {
      return std::nullopt;
    }
    if (negative && -value < std::numeric_limits<int>::min()) {
      return std::nullopt;
    }
  }

  auto const signed_value = negative ? -value : value;
  return static_cast<int>(signed_value);
}

/**
 * @brief 二重引用符で囲まれた文字列リテラルをパースする
 * @param s 入力文字列
 * @return クォートを除いた文字列（エスケープ展開済み）。形式不正なら std::nullopt
 * @details `\"` → `"`、`\\` → `\` の基本エスケープシーケンスを解釈する。
 */
[[nodiscard]] constexpr std::optional<std::string> parse_string_literal(std::string_view s) noexcept {
  if (s.size() < 2 || s.front() != '"' || s.back() != '"') {
    return std::nullopt;
  }
  auto inner = s.substr(1, s.size() - 2);
  std::string result;
  result.reserve(inner.size());
  for (std::size_t i = 0; i < inner.size(); ++i) {
    if (inner[i] == '\\' && i + 1 < inner.size()) {
      ++i;
      if (inner[i] == '"')       result += '"';
      else if (inner[i] == '\\') result += '\\';
      else { result += '\\'; result += inner[i]; }  // 未知のエスケープはそのまま
    } else {
      result += inner[i];
    }
  }
  return result;
}


/**
 * @brief 文字列の前後から空白（スペース・タブ）を取り除いた view を返す
 * @param s 入力文字列
 * @return トリム後の string_view
 * @note 元の文字列の所有権には影響しない。constexpr 対応。
 */
[[nodiscard]] constexpr std::string_view trim_sv(std::string_view s) noexcept {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
    s.remove_suffix(1);
  }
  return s;
}

/**
 * @brief GCC 16 constexpr ワークアラウンド用 find
 */
constexpr std::size_t constexpr_find(std::string_view haystack, std::string_view needle, std::size_t pos = 0) noexcept {
  if (pos >= haystack.size()) return std::string_view::npos;
  auto const* data = haystack.data();
  auto hsize = haystack.size();
  auto nsize = needle.size();
  if (nsize == 0) return pos;
  if (nsize > hsize - pos) return std::string_view::npos;
  auto const* ndata = needle.data();
  for (std::size_t i = pos; i + nsize <= hsize; ++i) {
    bool match = true;
    for (std::size_t j = 0; j < nsize; ++j) {
      if (data[i + j] != ndata[j]) { match = false; break; }
    }
    if (match) return i;
  }
  return std::string_view::npos;
}

constexpr std::size_t constexpr_find(std::string_view haystack, char needle, std::size_t pos = 0) noexcept {
  auto hsize = haystack.size();
  if (pos >= hsize) return std::string_view::npos;
  auto const* data = haystack.data();
  for (std::size_t i = pos; i < hsize; ++i) {
    if (data[i] == needle) return i;
  }
  return std::string_view::npos;
}

constexpr std::size_t constexpr_rfind(std::string_view haystack, char needle, std::size_t pos = std::string_view::npos) noexcept {
  auto hsize = haystack.size();
  if (hsize == 0) return std::string_view::npos;
  auto const* data = haystack.data();
  auto start = (pos >= hsize) ? hsize - 1 : pos;
  for (std::size_t i = start;; --i) {
    if (data[i] == needle) return i;
    if (i == 0) break;
  }
  return std::string_view::npos;
}

/**
 * @brief タグパターン探索（std::string 構築を避ける GCC 16 ワークアラウンド）
 *
 * @details "{{/" + key + "}}" のようなタグ文字列を探索する。
 *          std::string に string_view を渡すと GCC 16 の constexpr 評価で
 *          nullptr チェックに引っかかるため、std::string の一時オブジェクトを
 *          生成せずに 3 パートのタグを直接マッチする。
 */
constexpr std::size_t constexpr_find_tag(std::string_view haystack, std::string_view prefix, std::string_view key, std::string_view suffix, std::size_t pos) noexcept {
  auto hsize = haystack.size();
  auto plen = prefix.size();
  auto klen = key.size();
  auto slen = suffix.size();
  auto total = plen + klen + slen;
  if (pos + total > hsize) return std::string_view::npos;
  auto const* data = haystack.data();
  auto const* pdata = prefix.data();
  auto const* kdata = key.data();
  auto const* sdata = suffix.data();
  for (std::size_t i = pos; i + total <= hsize; ++i) {
    bool ok = true;
    for (std::size_t j = 0; j < plen; ++j) if (data[i + j] != pdata[j]) { ok = false; break; }
    if (!ok) continue;
    for (std::size_t j = 0; j < klen; ++j) if (data[i + plen + j] != kdata[j]) { ok = false; break; }
    if (!ok) continue;
    for (std::size_t j = 0; j < slen; ++j) if (data[i + plen + klen + j] != sdata[j]) { ok = false; break; }
    if (ok) return i;
  }
  return std::string_view::npos;
}

/**
 * @brief セクション開始タグ（フィルタ付きを含む）を探索する
 * @param haystack 探索対象のテンプレート
 * @param marker   開始タグのマーカー（"{{#" または "{{^"）
 * @param key      セクションキー
 * @param pos      探索開始位置
 * @return タグの開始位置。見つからなければ npos
 * @details "{{#key}}" に加えて "{{#key | filter}}" 形も同一キーの開始タグとして扱う。
 *          VM 側 (bytecode_compile.hpp) の閉じタグ探索はフィルタ付き開始タグも
 *          深さカウントに含めるため、CT 側も同じ一致規則に揃えないと
 *          同一キーのネストでパース結果が両者で食い違う。
 */
constexpr std::size_t constexpr_find_section_open(std::string_view haystack, std::string_view marker,
                                                  std::string_view key, std::size_t pos) noexcept {
  std::size_t i = pos;
  for (;;) {
    auto at = constexpr_find_tag(haystack, marker, key, std::string_view{}, i);
    if (at == std::string_view::npos) return std::string_view::npos;
    auto after = at + marker.size() + key.size();
    /** 直後が '}' なら "{{#key}}"、'|' か空白類なら "{{#key|filter}}" / "{{#key | filter}}"
     *  （いずれも開始タグ）。それ以外（例えば "{{#key2}}"）は別キーなのでスキップする。 */
    if (after < haystack.size()) {
      auto c = haystack[after];
      if (c == '}' || c == '|' || c == ' ' || c == '\t' || c == '~')
        return at;
    }
    i = at + 1;
  }
}

/**
 * @brief loop.X 予約語の有無を判定する
 * @param key プレースホルダーキー ("loop.index" / "loop.is_first" 等)
 * @return 該当する場合は対応する at_var_kind 列挙値、該当しない場合は std::nullopt
 * @details inja 互換の loop.* 予約語を判定する。
 *          "loop" 単体の参照は予約語ではない（オブジェクトとして存在しない）。
 *          "root" 単体の参照はフィールド参照として処理されるためここでは扱わない。
 */
[[nodiscard]] constexpr std::optional<at_var_kind> parse_loop_kind(std::string_view key) noexcept {
  if (key == "loop.index") {
    return at_var_kind::index;
  }
  if (key == "loop.index1") {
    return at_var_kind::index1;
  }
  if (key == "loop.size") {
    return at_var_kind::size;
  }
  if (key == "loop.is_first") {
    return at_var_kind::first;
  }
  if (key == "loop.is_last") {
    return at_var_kind::last;
  }
  if (key == "loop.is_even") {
    return at_var_kind::even;
  }
  if (key == "loop.is_odd") {
    return at_var_kind::odd;
  }
  if (key == "loop.key") {
    return at_var_kind::key;
  }
  return std::nullopt;
}

/**
 * @brief フィルタ名文字列を string_filter 列挙値に変換する
 * @param name フィルタ名（"to_upper" / "to_lower" / "trim"）
 * @return 対応する string_filter 列挙値、未知の場合は std::nullopt
 */
[[nodiscard]] constexpr std::optional<string_filter_entry> parse_string_filter(std::string_view name) noexcept {
  // 引数付きフィルタの処理
  auto paren = constexpr_find(name, '(');
  if (paren != std::string_view::npos && name.back() == ')') {
    auto fname    = name.substr(0, paren);
    auto args_str = name.substr(paren + 1, name.size() - paren - 2);
    int  arg1     = 0;
    int  arg2     = 0;
    // カンマで分割して引数を解析
    auto comma = constexpr_find(args_str, ',');
    if (comma != std::string_view::npos) {
      // 2引数
      arg1 = parse_int_arg(args_str.substr(0, comma));
      arg2 = parse_int_arg(args_str.substr(comma + 1));
    } else {
      // 1引数
      arg1 = parse_int_arg(args_str);
    }
    if (fname == "left")
      return string_filter_entry{string_filter::left, arg1, 0};
    if (fname == "right")
      return string_filter_entry{string_filter::right, arg1, 0};
    if (fname == "center")
      return string_filter_entry{string_filter::center, arg1, 0};
    if (fname == "repeat")
      return string_filter_entry{string_filter::repeat, arg1, 0};
    if (fname == "truncate")
      return string_filter_entry{string_filter::truncate, arg1, 0};
    if (fname == "substr")
      return string_filter_entry{string_filter::substr, arg1, arg2};
    if (fname == "replace") {
      /** replace(old,new): カンマで分割して文字列引数を保持（引用符は除去） */
      if (comma != std::string_view::npos) {
        auto old_str = trim_sv(args_str.substr(0, comma));
        auto new_str = trim_sv(args_str.substr(comma + 1));
        if (old_str.size() >= 2 && old_str.front() == '"' && old_str.back() == '"')
          old_str = old_str.substr(1, old_str.size() - 2);
        if (new_str.size() >= 2 && new_str.front() == '"' && new_str.back() == '"')
          new_str = new_str.substr(1, new_str.size() - 2);
        return string_filter_entry{string_filter::replace, 0, 0, old_str, new_str};
      }
      return string_filter_entry{string_filter::replace, 0, 0};
    }
    if (fname == "default") {
      auto def_str = trim_sv(args_str);
      if (def_str.size() >= 2 && def_str.front() == '"' && def_str.back() == '"') {
        def_str = def_str.substr(1, def_str.size() - 2);
      }
      return string_filter_entry{string_filter::default_value, 0, 0, def_str, {}};
    }
    if (fname == "indent") {
      arg1 = parse_int_arg(args_str);
      return string_filter_entry{string_filter::indent, arg1, 0};
    }
    if (fname == "pad") {
      if (comma != std::string_view::npos) {
        arg1 = parse_int_arg(args_str.substr(0, comma));
        auto pad_str = trim_sv(args_str.substr(comma + 1));
        if (pad_str.size() >= 2 && pad_str.front() == '"' && pad_str.back() == '"') {
          pad_str = pad_str.substr(1, pad_str.size() - 2);
        }
        return string_filter_entry{string_filter::pad, arg1, 0, pad_str, {}};
      }
      arg1 = parse_int_arg(args_str);
      return string_filter_entry{string_filter::pad, arg1, 0};
    }
    if (fname == "pluralize") {
      if (comma != std::string_view::npos) {
        auto singular = trim_sv(args_str.substr(0, comma));
        auto plural   = trim_sv(args_str.substr(comma + 1));
        if (singular.size() >= 2 && singular.front() == '"' && singular.back() == '"') {
          singular = singular.substr(1, singular.size() - 2);
        }
        if (plural.size() >= 2 && plural.front() == '"' && plural.back() == '"') {
          plural = plural.substr(1, plural.size() - 2);
        }
        return string_filter_entry{string_filter::pluralize, 0, 0, singular, plural};
      }
      return string_filter_entry{string_filter::pluralize, 0, 0};
    }
    if (fname == "format") {
      auto fmt_str = trim_sv(args_str);
      if (fmt_str.size() >= 2 && fmt_str.front() == '"' && fmt_str.back() == '"') {
        fmt_str = fmt_str.substr(1, fmt_str.size() - 2);
      }
      return string_filter_entry{string_filter::format, 0, 0, fmt_str, {}};
    }
    // trim/lstrip/rstrip と strip/lstrip/rstrip: 除去する文字集合（引用符除去して保持、省略時は Python 空白セット）
    auto make_strip = [&](string_filter kind) {
      auto chars = trim_sv(args_str);
      if (chars.size() >= 2 && chars.front() == '"' && chars.back() == '"') {
        chars = chars.substr(1, chars.size() - 2);
      }
      return string_filter_entry{kind, 0, 0, chars, {}};
    };
    if (fname == "trim")
      return make_strip(string_filter::trim);
    if (fname == "ltrim")
      return make_strip(string_filter::ltrim);
    if (fname == "rtrim")
      return make_strip(string_filter::rtrim);
    if (fname == "strip")
      return make_strip(string_filter::trim);
    if (fname == "lstrip")
      return make_strip(string_filter::ltrim);
    if (fname == "rstrip")
      return make_strip(string_filter::rtrim);
  }
  // 引数なしフィルタ
  if (name == "upper")
    return string_filter_entry{string_filter::upper, 0, 0};
  if (name == "lower")
    return string_filter_entry{string_filter::lower, 0, 0};
  if (name == "capitalize")
    return string_filter_entry{string_filter::capitalize, 0, 0};
  if (name == "title")
    return string_filter_entry{string_filter::title, 0, 0};
  if (name == "trim")
    return string_filter_entry{string_filter::trim, 0, 0};
  if (name == "ltrim")
    return string_filter_entry{string_filter::ltrim, 0, 0};
  if (name == "rtrim")
    return string_filter_entry{string_filter::rtrim, 0, 0};
  // strip/lstrip/rstrip は trim/ltrim/rtrim のエイリアス（Python 準拠の空白除去）
  if (name == "strip")
    return string_filter_entry{string_filter::trim, 0, 0};
  if (name == "lstrip")
    return string_filter_entry{string_filter::ltrim, 0, 0};
  if (name == "rstrip")
    return string_filter_entry{string_filter::rtrim, 0, 0};
  if (name == "replace")
    return string_filter_entry{string_filter::replace, 0, 0};
  if (name == "json")
    return string_filter_entry{string_filter::to_json, 0, 0};
  if (name == "safe")
    return string_filter_entry{string_filter::safe, 0, 0};
  if (name == "urlencode")
    return string_filter_entry{string_filter::urlencode, 0, 0};
  return std::nullopt;
}

/**
 * @brief フィルタ名文字列を int_filter 列挙値に変換する
 * @param name フィルタ名（"abs" / "hex" / "oct" / "bin"）
 * @return 対応する int_filter 列挙値、未知の場合は std::nullopt
 */
[[nodiscard]] constexpr std::optional<int_filter_entry> parse_int_filter(std::string_view name) noexcept {
  // 引数付きフィルタの処理
  auto paren = constexpr_find(name, '(');
  if (paren != std::string_view::npos && name.back() == ')') {
    auto fname   = name.substr(0, paren);
    auto arg_str = name.substr(paren + 1, name.size() - paren - 2);
    int  arg     = parse_int_arg(arg_str);
    if (fname == "mod")
      return int_filter_entry{int_filter::mod, arg};
    if (fname == "eq")
      return int_filter_entry{int_filter::eq, arg};
    if (fname == "ne")
      return int_filter_entry{int_filter::ne, arg};
    if (fname == "gt")
      return int_filter_entry{int_filter::gt, arg};
    if (fname == "gte")
      return int_filter_entry{int_filter::gte, arg};
    if (fname == "lt")
      return int_filter_entry{int_filter::lt, arg};
    if (fname == "lte")
      return int_filter_entry{int_filter::lte, arg};
    if (fname == "zerofill")
      return int_filter_entry{int_filter::zerofill, arg};
    if (fname == "add")
      return int_filter_entry{int_filter::add, arg};
    if (fname == "sub")
      return int_filter_entry{int_filter::sub, arg};
    if (fname == "mul")
      return int_filter_entry{int_filter::mul, arg};
    if (fname == "div")
      return int_filter_entry{int_filter::div, arg};
  }
  // 引数なしフィルタ
  if (name == "abs")
    return int_filter_entry{int_filter::abs, 0};
  if (name == "hex")
    return int_filter_entry{int_filter::hex, 0};
  if (name == "oct")
    return int_filter_entry{int_filter::oct, 0};
  if (name == "bin")
    return int_filter_entry{int_filter::bin, 0};
  if (name == "neg")
    return int_filter_entry{int_filter::neg, 0};
  if (name == "numify")
    return int_filter_entry{int_filter::numify, 0};
  if (name == "is_neg")
    return int_filter_entry{int_filter::is_neg, 0};
  return std::nullopt;
}

/** @brief セクションフィルタオペコード（{{#key | reverse}} / {{#key | take(n)}} / {{#key | skip(n)}} / ...） */
struct section_filter_op {
  section_filter_op_kind kind;
  int arg = 0;  /**< take/skip/take_last/skip_last の引数、stride の取得数 */
  int arg2 = 0; /**< stride のスキップ数 */
  std::string_view str_arg1; /**< join の separator 文字列 */
};

/** @brief セクションフィルタ名を解析する */
[[nodiscard]] constexpr std::optional<section_filter_op> parse_section_filter(std::string_view name) noexcept {
  if (name == "reverse")
    return section_filter_op{section_filter_op_kind::reverse, 0};
  auto paren = constexpr_find(name, '(');
  if (paren != std::string_view::npos && name.back() == ')') {
    auto fn = name.substr(0, paren);
    auto arg_str = name.substr(paren + 1, name.size() - paren - 2);
    if (fn == "stride") {
      // stride(t,s): t 個取得して s 個飛ばす繰り返し
      auto comma = constexpr_find(arg_str, ',');
      if (comma == std::string_view::npos)
        return section_filter_op{section_filter_op_kind::stride, parse_int_arg(arg_str), 0};
      return section_filter_op{section_filter_op_kind::stride, parse_int_arg(arg_str.substr(0, comma)),
                               parse_int_arg(arg_str.substr(comma + 1))};
    }
    if (fn == "join") {
      // join(separator): 区切り文字列を保持
      auto sep = trim_sv(arg_str);
      if (sep.size() >= 2 && sep.front() == '"' && sep.back() == '"') {
        sep = sep.substr(1, sep.size() - 2);
      }
      return section_filter_op{section_filter_op_kind::join, 0, 0, sep};
    }
    auto arg = parse_int_arg(arg_str);
    if (fn == "take") return section_filter_op{section_filter_op_kind::take, arg};
    if (fn == "skip") return section_filter_op{section_filter_op_kind::skip, arg};
    if (fn == "take_last") return section_filter_op{section_filter_op_kind::take_last, arg};
    if (fn == "skip_last") return section_filter_op{section_filter_op_kind::skip_last, arg};
  }
  return std::nullopt;
}

/**
 * @brief フィルタ名文字列を float_filter 列挙値に変換する
 * @param name フィルタ名（"numify" / "precision" / "abs" / "neg"）
 * @return 対応する float_filter 列挙値、未知の場合は std::nullopt
 */
[[nodiscard]] constexpr std::optional<float_filter_entry> parse_float_filter(std::string_view name) noexcept {
  auto paren = constexpr_find(name, '(');
  if (paren != std::string_view::npos && name.back() == ')') {
    auto fname   = name.substr(0, paren);
    auto arg_str = name.substr(paren + 1, name.size() - paren - 2);
    int  arg     = parse_int_arg(arg_str);
    if (fname == "precision")
      return float_filter_entry{float_filter::precision, arg};
    if (fname == "round")
      return float_filter_entry{float_filter::round, arg};
  }
  if (name == "round")
    return float_filter_entry{float_filter::round, 0};
  return std::nullopt;
}

/**
 * @brief if ボディ内のトップレベル {{else}} の位置を検索する
 * @param body if ボディ文字列
 * @return {{else}} の開始位置、見つからない場合は std::string_view::npos
 * @details ネストされたセクション（{{#...}}）や if（{{#if...}}）内の else は
 *          深度カウントにより無視する。{{#if}} と {{#section}} は別カウンタで
 *          管理し、depth 0 でのみ {{else}} をトップレベルと判定する。
 */
[[nodiscard]] constexpr std::size_t find_toplevel_else(std::string_view body) noexcept {
  std::size_t pos           = 0;
  int         if_depth      = 0;
  int         section_depth = 0;
  while (pos < body.size()) {
    auto tag_pos = constexpr_find(body, "{{", pos);
    if (tag_pos == std::string_view::npos) {
      break;
    }
    auto end = constexpr_find(body, "}}", tag_pos + 2);
    if (end == std::string_view::npos) {
      break;
    }
    auto tag_inner_raw = body.substr(tag_pos + 2, end - tag_pos - 2);
    bool leading_tilde  = tag_inner_raw.starts_with("~");
    bool trailing_tilde = tag_inner_raw.ends_with("~") && tag_inner_raw.size() > 1;
    auto tag_inner = trim_sv(tag_inner_raw);
    // チルダ付きタグ ({{~else~}} 等) を本体パース 348-356 行と同じ規則で正規化
    if (leading_tilde && tag_inner.size() > 1 && tag_inner.front() == '~')
      tag_inner = trim_sv(tag_inner.substr(1));
    if (trailing_tilde && tag_inner.size() > 1 && tag_inner.back() == '~')
      tag_inner = trim_sv(tag_inner.substr(0, tag_inner.size() - 1));
    if (tag_inner.starts_with("#if") && (tag_inner.size() == 3 || tag_inner[3] == ' ' || tag_inner[3] == '\t')) {
      ++if_depth;
    } else if (tag_inner.starts_with("#")) {
      ++section_depth;
    } else if (tag_inner.starts_with("^")) {
      ++section_depth;
    } else if (tag_inner.starts_with("/if") && (tag_inner.size() == 3 || tag_inner[3] == ' ' || tag_inner[3] == '\t')) {
      if (if_depth > 0) {
        --if_depth;
      } else if (section_depth > 0) {
        --section_depth;
      }
    } else if (tag_inner.starts_with("/")) {
      if (section_depth > 0) {
        --section_depth;
      } else if (if_depth > 0) {
        --if_depth;
      }
    } else if (tag_inner == "else" && if_depth == 0 && section_depth == 0) {
      return tag_pos;
    }
    pos = end + 2;
  }
  return std::string_view::npos;
}

/** @brief split_by_pipe の結果を保持する固定サイズコンテナ（ヒープ割り当てなし） */
struct pipe_parts {
  static constexpr std::size_t            max_parts = 5; /**< 変数名 + 最大4フィルタ */
  std::array<std::string_view, max_parts> data{};
  std::size_t                             count = 0;

  [[nodiscard]] constexpr std::string_view const& operator[](std::size_t i) const noexcept { return data[i]; }
  [[nodiscard]] constexpr std::size_t             size() const noexcept { return count; }
  [[nodiscard]] constexpr bool                    empty() const noexcept { return count == 0; }
};

/**
 * @brief 文字列を '|' で分割し、各パートの前後空白を除去する
 * @param input 入力文字列
 * @return 分割された文字列の固定サイズ配列
 */
[[nodiscard]] constexpr pipe_parts split_by_pipe(std::string_view input) noexcept {
  pipe_parts  result;
  std::size_t pos = 0;
  while (pos < input.size() && result.count < pipe_parts::max_parts) {
    auto pipe = constexpr_find(input, '|', pos);
    if (pipe == std::string_view::npos) {
      result.data[result.count++] = trim_sv(input.substr(pos));
      break;
    }
    result.data[result.count++] = trim_sv(input.substr(pos, pipe - pos));
    pos                         = pipe + 1;
  }
  return result;
}

constexpr int max_var_depth = 100;

/**
 * @brief 前処理後のオフセットを元テンプレートのオフセットへ変換する写像
 *
 * @details コメント除去・タグ整形・@var 展開はテンプレート長を変えるため、
 *          エラー位置をそのまま返すと元ソースとずれる。変換時に「出力範囲 →
 *          入力範囲」のセグメントを記録し、エラー発生時に逆引きする。
 */
struct offset_map {
  /** @brief 長さが変わる地点。変更の無い区間は map() が差分シフトで処理する */
  struct change {
    std::size_t dst;      /**< 出力側の開始オフセット */
    std::size_t src;      /**< 入力側の開始オフセット */
    std::size_t dst_len;  /**< 出力側の長さ（削除なら 0） */
    std::size_t src_len;  /**< 入力側の長さ */
  };
  std::vector<change> changes;

  void add(std::size_t dst, std::size_t src, std::size_t dst_len, std::size_t src_len) {
    if (dst_len == 0 && src_len == 0) return;
    changes.push_back({dst, src, dst_len, src_len});
  }

  /** @brief 出力オフセットを入力オフセットへ変換する */
  [[nodiscard]] std::size_t map(std::size_t p) const noexcept {
    if (changes.empty()) return p;  // 長さ不変なら恒等写像
    std::size_t lo = 0;
    std::size_t hi = changes.size();
    while (lo < hi) {
      auto mid = (lo + hi) / 2;
      if (changes[mid].dst <= p) lo = mid + 1;
      else hi = mid;
    }
    if (lo == 0) return p;  // 最初の変更より前は恒等
    auto const& c = changes[lo - 1];
    if (c.dst_len != 0 && p < c.dst + c.dst_len) {
      auto const delta = p - c.dst;
      return c.src + (delta < c.src_len ? delta : c.src_len);
    }
    // 変更区間の後ろ: 累積シフトを適用
    return p + (c.src + c.src_len) - (c.dst + c.dst_len);
  }
};

/** @brief 変換結果と、出力→入力のオフセットマップ */
struct mapped_text {
  std::string text;
  offset_map  map;

  void reserve(std::size_t n) { text.reserve(n); }

  /** @brief 入力の一部をそのままコピーする（マップ不要） */
  void copy(std::string_view src, std::size_t) {
    if (src.empty()) return;
    (void)hot_append(text, src);
  }

  /** @brief 入力 1 文字をそのままコピーする（マップ不要） */
  void copy_char(char c, std::size_t) { text.push_back(c); }

  /** @brief 入力範囲を削除する（長さが変わる地点を記録） */
  void remove(std::size_t src_off, std::size_t src_len) {
    if (src_len == 0) return;
    map.add(text.size(), src_off, 0, src_len);
  }

  /** @brief 入力を別の文字列に書き換えて追記する */
  void rewrite(std::string_view out, std::size_t src_off, std::size_t src_len) {
    map.add(text.size(), src_off, out.size(), src_len);
    if (!out.empty()) (void)hot_append(text, out);
  }
};

template <class ConstMap>
[[nodiscard]] expected<std::string> expand_var_refs_cycle_safe(std::string_view content, ConstMap const& consts,
                                                                std::unordered_set<std::string>& visited, int depth = 0,
                                                                std::size_t base = 0) {
  if (depth > max_var_depth) {
    return std::unexpected(error_ctx{0, error_code::syntax_error, "circular @var reference"});
  }

  std::string result;
  std::size_t pos = 0;

  while (pos < content.size()) {
    auto var_start = content.find("@var(", pos);
    if (var_start == std::string_view::npos) {
      (void)hot_append(result, content.substr(pos));
      break;
    }

    (void)hot_append(result, content.substr(pos, var_start - pos));

    auto paren_start = var_start + 5;
    auto paren_end   = content.find(')', paren_start);
    if (paren_end == std::string_view::npos) {
      (void)hot_append(result, content.substr(var_start));
      break;
    }

    auto name = std::string{content.substr(paren_start, paren_end - paren_start)};
    if (visited.contains(name)) {
      return std::unexpected(error_ctx{base + var_start, error_code::syntax_error, "circular @var reference (detected)"});
    }
    auto it = consts.find(name);
    if (it == consts.end()) {
      return std::unexpected(error_ctx{base + var_start, error_code::unknown_key, "undefined @var constant"});
    }

    visited.insert(name);
    auto expanded = expand_var_refs_cycle_safe(std::string_view{it->second}, consts, visited, depth + 1);
    visited.erase(name);
    if (!expanded) {
      /** 定数値の展開中に発生したエラーは、呼び出し元の @var タグ位置を指す */
      auto err = expanded.error();
      err.position = base + var_start;
      return std::unexpected(err);
    }
    result += *expanded;

    pos = paren_end + 1;
  }

  return result;
}

template <class ConstMap>
[[nodiscard]] expected<mapped_text> expand_vars_in_template(std::string_view tmpl, ConstMap const& consts) {
  mapped_text out;
  std::size_t pos = 0;
  std::unordered_set<std::string> visited;

  while (pos < tmpl.size()) {
    auto tag_start = tmpl.find("{{", pos);
    if (tag_start == std::string_view::npos) {
      out.copy(tmpl.substr(pos), pos);
      break;
    }

    out.copy(tmpl.substr(pos, tag_start - pos), pos);

    if (tag_start + 2 < tmpl.size() && tmpl[tag_start + 2] == '{') {
      auto raw_end = tmpl.find("}}}", tag_start + 3);
      if (raw_end == std::string_view::npos) {
        out.copy(tmpl.substr(tag_start), tag_start);
        break;
      }
      auto raw_inner = tmpl.substr(tag_start + 3, raw_end - tag_start - 3);
      if (raw_inner.find("@var(") == std::string_view::npos) {
        out.copy(tmpl.substr(tag_start, raw_end + 3 - tag_start), tag_start);
        pos = raw_end + 3;
        continue;
      }
      auto expanded  = expand_var_refs_cycle_safe(raw_inner, consts, visited, 0, tag_start + 3);
      if (!expanded) {
        return std::unexpected(expanded.error());
      }
      auto        raw_trim = trim_sv(*expanded);
      std::string rebuilt;
      if (raw_trim.starts_with("{{{") || raw_trim.starts_with("{{")) {
        rebuilt.assign(raw_trim);
      } else {
        rebuilt.reserve(expanded->size() + 6);
        rebuilt += "{{{";
        rebuilt += *expanded;
        rebuilt += "}}}";
      }
      auto const orig_tag = tmpl.substr(tag_start, raw_end + 3 - tag_start);
      if (std::string_view{rebuilt} == orig_tag) out.copy(orig_tag, tag_start);
      else out.rewrite(rebuilt, tag_start, raw_end + 3 - tag_start);
      pos = raw_end + 3;
      continue;
    }

    auto tag_end = tmpl.find("}}", tag_start + 2);
    if (tag_end == std::string_view::npos) {
      out.copy(tmpl.substr(tag_start), tag_start);
      break;
    }

    auto inner    = tmpl.substr(tag_start + 2, tag_end - tag_start - 2);
    if (inner.find("@var(") == std::string_view::npos) {
      out.copy(tmpl.substr(tag_start, tag_end + 2 - tag_start), tag_start);
      pos = tag_end + 2;
      continue;
    }
    auto expanded = expand_var_refs_cycle_safe(inner, consts, visited, 0, tag_start + 2);
    if (!expanded) {
      return std::unexpected(expanded.error());
    }

    auto        norm_trim = trim_sv(*expanded);
    std::string rebuilt;
    if (norm_trim.starts_with("{{")) {
      rebuilt.assign(norm_trim);
    } else {
      rebuilt.reserve(expanded->size() + 4);
      rebuilt += "{{";
      rebuilt += *expanded;
      rebuilt += "}}";
    }

    auto const orig_tag = tmpl.substr(tag_start, tag_end + 2 - tag_start);
    if (std::string_view{rebuilt} == orig_tag) out.copy(orig_tag, tag_start);
    else out.rewrite(rebuilt, tag_start, tag_end + 2 - tag_start);
    pos = tag_end + 2;
  }

  return out;
}

[[nodiscard]] inline mapped_text strip_standalone_whitespace_tildes(std::string_view tmpl) {
  mapped_text out;
  // A1: {{ を含まない場合は早期 return (コピー1回のみ)
  if (tmpl.find("{{") == std::string_view::npos) {
    out.copy(tmpl, 0);
    return out;
  }
  out.reserve(tmpl.size());
  std::size_t pos = 0;
  while (pos < tmpl.size()) {
    auto tag = tmpl.find("{{", pos);
    if (tag == std::string_view::npos) {
      out.copy(tmpl.substr(pos), pos);
      break;
    }
    if (tag > 0 && tmpl[tag - 1] == '{') {
      out.copy(tmpl.substr(pos, tag - pos + 1), pos);
      pos = tag + 1;
      continue;
    }
    out.copy(tmpl.substr(pos, tag - pos), pos);
    auto end = tmpl.find("}}", tag + 2);
    if (end == std::string_view::npos) {
      out.copy(tmpl.substr(tag), tag);
      break;
    }
    std::size_t inner_start = tag + 2;
    std::size_t inner_end = end;
    bool leading_tilde = (inner_start < inner_end && tmpl[inner_start] == '~');
    bool trailing_tilde = (inner_end > inner_start && tmpl[inner_end - 1] == '~');
    if (leading_tilde) ++inner_start;
    if (trailing_tilde && inner_end > inner_start) --inner_end;
    auto inner = tmpl.substr(inner_start, inner_end - inner_start);
    auto trimmed = trim_sv(inner);
    bool changed = leading_tilde || trailing_tilde || trimmed.size() != inner.size();
    if (!changed) {
      out.copy(tmpl.substr(tag, end + 2 - tag), tag);
    } else {
      out.map.add(out.text.size(), tag, trimmed.size() + 4, end + 2 - tag);
      out.text += "{{";
      out.text += trimmed;
      out.text += "}}";
    }
    pos = end + 2;
  }
  return out;
}

[[nodiscard]] inline mapped_text transform_exists_sections(std::string_view tmpl) {
  mapped_text out;
  // A2: {{ を含まない場合、または exists 構文が無い場合は早期 return
  if (tmpl.find("{{") == std::string_view::npos ||
      (tmpl.find("#exists") == std::string_view::npos && tmpl.find("^exists") == std::string_view::npos)) {
    out.copy(tmpl, 0);
    return out;
  }
  out.reserve(tmpl.size());
  /** 開いた exists セクションの変数名スタック。{{/exists}} を対応する変数名へ書き換える */
  std::vector<std::string_view> exists_stack;
  std::size_t pos = 0;
  while (pos < tmpl.size()) {
    auto tag = tmpl.find("{{", pos);
    if (tag == std::string_view::npos) {
      out.copy(tmpl.substr(pos), pos);
      break;
    }
    out.copy(tmpl.substr(pos, tag - pos), pos);
    auto end = tmpl.find("}}", tag + 2);
    if (end == std::string_view::npos) {
      out.copy(tmpl.substr(tag), tag);
      break;
    }
    auto const tag_len = end - tag + 2;
    auto inner = tmpl.substr(tag + 2, end - tag - 2);
    auto trimmed = trim_sv(inner);
    if (trimmed == "#exists" || trimmed.starts_with("#exists ")) {
      auto rest = trim_sv(trimmed.substr(7));
      // exists は単一フィールド名のみを扱う。空/複数トークンは推定不能なため、
      // 埋め込み変換を行わず元のタグをそのまま保持する。
      if (rest.empty() || rest.find_first_of(" \t\r\n") != std::string_view::npos) {
        out.copy(tmpl.substr(tag, tag_len), tag);
        pos = end + 2;
        continue;
      }
      exists_stack.push_back(rest);
      std::string rebuilt = "{{#";
      rebuilt += rest;
      rebuilt += " }}";
      out.rewrite(rebuilt, tag, tag_len);
      pos = end + 2;
      continue;
    } else if (trimmed == "^exists" || trimmed.starts_with("^exists ")) {
      auto rest = trim_sv(trimmed.substr(8));
      if (rest.empty() || rest.find_first_of(" \t\r\n") != std::string_view::npos) {
        out.copy(tmpl.substr(tag, tag_len), tag);
        pos = end + 2;
        continue;
      }
      exists_stack.push_back(rest);
      std::string rebuilt = "{{^";
      rebuilt += rest;
      rebuilt += " }}";
      out.rewrite(rebuilt, tag, tag_len);
      pos = end + 2;
      continue;
    } else if (trimmed == "/exists" && !exists_stack.empty() && !exists_stack.back().empty()) {
      /** 閉じタグ {{/exists}} を対応する変数名で閉じる（閉じタグ名一致検証のため） */
      auto name = exists_stack.back();
      exists_stack.pop_back();
      std::string rebuilt = "{{/";
      rebuilt += name;
      rebuilt += "}}";
      out.rewrite(rebuilt, tag, tag_len);
      pos = end + 2;
      continue;
    } else {
      out.copy(tmpl.substr(tag, tag_len), tag);
      pos = end + 2;
      continue;
    }
  }
  return out;
}

[[nodiscard]] inline mapped_text strip_bang_comments(std::string_view tmpl) {
  mapped_text out;
  if (tmpl.find("{{!") == std::string_view::npos) {
    out.copy(tmpl, 0);
    return out;
  }
  out.reserve(tmpl.size());
  std::size_t pos = 0;
  while (pos < tmpl.size()) {
    auto cs = tmpl.find("{{!", pos);
    if (cs == std::string_view::npos) {
      out.copy(tmpl.substr(pos), pos);
      break;
    }
    if (cs > 0 && tmpl[cs - 1] == '{') {
      out.copy(tmpl.substr(pos, cs - pos + 1), pos);
      pos = cs + 1;
      continue;
    }
    out.copy(tmpl.substr(pos, cs - pos), pos);
    // ネストした {{ }} を考慮してコメント終端を探す（CT パーサ ct_parse_into と同じ規則）。
    // {{! {{name}} }} のようにコメント内にタグがあっても外側の }} で閉じる。
    std::size_t comment_pos = cs + 3;
    bool        closed      = false;
    while (comment_pos < tmpl.size()) {
      auto next_open  = tmpl.find("{{", comment_pos);
      auto next_close = tmpl.find("}}", comment_pos);
      if (next_close == std::string_view::npos) break;
      if (next_open != std::string_view::npos && next_open < next_close) {
        auto inner_close = tmpl.find("}}", next_open + 2);
        if (inner_close == std::string_view::npos) break;
        comment_pos = inner_close + 2;
      } else {
        out.remove(cs, next_close + 2 - cs);
        pos = next_close + 2;
        closed = true;
        break;
      }
    }
    if (!closed) {
      out.copy_char('{', cs);
      pos = cs + 1;
    }
  }
  return out;
}

[[nodiscard]] inline mapped_text strip_comments(std::string_view tmpl) {
  mapped_text out;
  if (tmpl.find("{#") == std::string_view::npos) {
    out.copy(tmpl, 0);
    return out;
  }
  out.reserve(tmpl.size());
  std::size_t pos = 0;
  while (pos < tmpl.size()) {
    auto cs = tmpl.find("{#", pos);
    if (cs == std::string_view::npos) {
      out.copy(tmpl.substr(pos), pos);
      break;
    }
    if (cs > 0 && tmpl[cs - 1] == '{') {
      out.copy(tmpl.substr(pos, cs - pos + 1), pos);
      pos = cs + 1;
      continue;
    }
    out.copy(tmpl.substr(pos, cs - pos), pos);
    auto ce = tmpl.find("#}", cs + 2);
    if (ce == std::string_view::npos) {
      out.copy_char('{', cs);
      pos = cs + 1;
    } else {
      out.remove(cs, ce + 2 - cs);
      pos = ce + 2;
    }
  }
  return out;
}

[[nodiscard]] constexpr std::string_view trim_tail_whitespace_for_lstrip(std::string_view sv) noexcept {
  auto nl = constexpr_rfind(sv, '\n');
  if (nl == std::string_view::npos) {
    for (auto c : sv) {
      if (c != ' ' && c != '\t' && c != '\r') return sv;
    }
    return {};
  }
  for (std::size_t i = nl + 1; i < sv.size(); ++i) {
    if (sv[i] != ' ' && sv[i] != '\t' && sv[i] != '\r') return sv;
  }
  return sv.substr(0, nl + 1);
}

[[nodiscard]] constexpr bool is_block_tag_start(std::string_view tmpl, std::size_t tag_start) noexcept {
  if (tag_start + 3 >= tmpl.size()) return false;
  auto c = tmpl[tag_start + 2];
  if (c == '#' || c == '^' || c == '/') return true;
  if (c == 'e' && tag_start + 6 <= tmpl.size()) {
    return tmpl[tag_start + 2] == 'e' && tmpl[tag_start + 3] == 'l' &&
           tmpl[tag_start + 4] == 's' && tmpl[tag_start + 5] == 'e';
  }
  return false;
}

constexpr std::size_t constexpr_find_close_partialdef(std::string_view sv, std::size_t pos) noexcept {
  std::size_t depth = 1;
  while (depth > 0) {
    auto next_open  = constexpr_find(sv, std::string_view("{{#partialdef "), pos);
    auto next_close = constexpr_find(sv, std::string_view("{{/partialdef}}"), pos);
    if (next_close == std::string_view::npos)
      return std::string_view::npos;
    if (next_open != std::string_view::npos && next_open < next_close) {
      ++depth;
      pos = next_open + 14;
    } else {
      --depth;
      if (depth == 0)
        return next_close;
      pos = next_close + 15;
    }
  }
  return std::string_view::npos;
}

/**
 * @brief partial 参照（{{#partial}} / {{> }} / {{#partiallocal}}）を列挙する
 *
 * @details 空白の有無（{{>a}} と {{> a}}）を同一視し、コールバックに参照名と
 *          参照タグの開始オフセットを渡す。
 */
template <class F>
constexpr void for_each_partial_ref(std::string_view body, F&& f) {
  std::size_t pos = 0;
  while (pos < body.size()) {
    auto at = constexpr_find(body, std::string_view("{{"), pos);
    if (at == std::string_view::npos)
      break;
    auto end = constexpr_find(body, std::string_view("}}"), at + 2);
    if (end == std::string_view::npos)
      break;
    auto inner = trim_sv(body.substr(at + 2, end - at - 2));
    std::string_view ref{};
    if (inner.starts_with("#partiallocal "))
      ref = trim_sv(inner.substr(14));
    else if (inner.starts_with("#partial "))
      ref = trim_sv(inner.substr(9));
    else if (inner.starts_with(">"))
      ref = trim_sv(inner.substr(1));
    if (!ref.empty())
      f(ref, at);
    pos = end + 2;
  }
}

}  // namespace injamm::detail
