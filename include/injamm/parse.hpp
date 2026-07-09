#pragma once

#include "types.hpp"
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

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
    if (fname == "truncate")
      return string_filter_entry{string_filter::truncate, arg1, 0};
    if (fname == "substr")
      return string_filter_entry{string_filter::substr, arg1, arg2};
    if (fname == "replace") {
      /** replace(old,new): カンマで分割して文字列引数を保持 */
      if (comma != std::string_view::npos) {
        auto old_str = trim_sv(args_str.substr(0, comma));
        auto new_str = trim_sv(args_str.substr(comma + 1));
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
  if (name == "replace")
    return string_filter_entry{string_filter::replace, 0, 0};
  if (name == "json")
    return string_filter_entry{string_filter::to_json, 0, 0};
  if (name == "safe")
    return string_filter_entry{string_filter::safe, 0, 0};
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
  }
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
    auto tag_inner = trim_sv(body.substr(tag_pos + 2, end - tag_pos - 2));
    if (tag_inner.starts_with("#if")) {
      ++if_depth;
    } else if (tag_inner.starts_with("#")) {
      ++section_depth;
    } else if (tag_inner.starts_with("/if")) {
      if (if_depth > 0) {
        --if_depth;
      }
    } else if (tag_inner.starts_with("/")) {
      if (section_depth > 0) {
        --section_depth;
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
[[nodiscard]] constexpr pipe_parts split_by_pipe(std::string_view input) {
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

template <class ConstMap>
[[nodiscard]] expected<std::string> expand_var_refs_cycle_safe(std::string_view content, ConstMap const& consts,
                                                                std::unordered_set<std::string>& visited, int depth = 0) {
  if (depth > max_var_depth) {
    return std::unexpected(error_ctx{0, error_code::syntax_error, "circular @var reference"});
  }

  std::string result;
  std::size_t pos = 0;

  while (pos < content.size()) {
    auto var_start = content.find("@var(", pos);
    if (var_start == std::string_view::npos) {
      result.append(content.substr(pos));
      break;
    }

    result.append(content.substr(pos, var_start - pos));

    auto paren_start = var_start + 5;
    auto paren_end   = content.find(')', paren_start);
    if (paren_end == std::string_view::npos) {
      result.append(content.substr(var_start));
      break;
    }

    auto name = std::string{content.substr(paren_start, paren_end - paren_start)};
    if (visited.contains(name)) {
      return std::unexpected(error_ctx{var_start, error_code::syntax_error, "circular @var reference (detected)"});
    }
    auto it = consts.find(name);
    if (it == consts.end()) {
      return std::unexpected(error_ctx{var_start, error_code::unknown_key, "undefined @var constant"});
    }

    visited.insert(name);
    auto expanded = expand_var_refs_cycle_safe(std::string_view{it->second}, consts, visited, depth + 1);
    visited.erase(name);
    if (!expanded) {
      return std::unexpected(expanded.error());
    }
    result += *expanded;

    pos = paren_end + 1;
  }

  return result;
}

template <class ConstMap>
[[nodiscard]] expected<std::string> expand_vars_in_template(std::string_view tmpl, ConstMap const& consts) {
  std::string result;
  std::size_t pos = 0;
  std::unordered_set<std::string> visited;

  while (pos < tmpl.size()) {
    auto tag_start = tmpl.find("{{", pos);
    if (tag_start == std::string_view::npos) {
      result.append(tmpl.substr(pos));
      break;
    }

    result.append(tmpl.substr(pos, tag_start - pos));

    if (tag_start + 2 < tmpl.size() && tmpl[tag_start + 2] == '{') {
      auto raw_end = tmpl.find("}}}", tag_start + 3);
      if (raw_end == std::string_view::npos) {
        result.append(tmpl.substr(tag_start));
        break;
      }
      auto raw_inner = tmpl.substr(tag_start + 3, raw_end - tag_start - 3);
      auto expanded  = expand_var_refs_cycle_safe(raw_inner, consts, visited);
      if (!expanded) {
        return std::unexpected(expanded.error());
      }
      result += "{{{";
      result += *expanded;
      result += "}}}";
      pos = raw_end + 3;
      continue;
    }

    auto tag_end = tmpl.find("}}", tag_start + 2);
    if (tag_end == std::string_view::npos) {
      result.append(tmpl.substr(tag_start));
      break;
    }

    auto inner    = tmpl.substr(tag_start + 2, tag_end - tag_start - 2);
    auto expanded = expand_var_refs_cycle_safe(inner, consts, visited);
    if (!expanded) {
      return std::unexpected(expanded.error());
    }

    result += "{{";
    result += *expanded;
    result += "}}";

    pos = tag_end + 2;
  }

  return result;
}

[[nodiscard]] inline std::string strip_standalone_whitespace_tildes(std::string_view tmpl) {
  std::string result;
  result.reserve(tmpl.size());
  std::size_t pos = 0;
  while (pos < tmpl.size()) {
    auto tag = tmpl.find("{{", pos);
    if (tag == std::string_view::npos) {
      result.append(tmpl.substr(pos));
      break;
    }
    if (tag > 0 && tmpl[tag - 1] == '{') {
      result.append(tmpl.substr(pos, tag - pos + 1));
      pos = tag + 1;
      continue;
    }
    result.append(tmpl.substr(pos, tag - pos));
    auto end = tmpl.find("}}", tag + 2);
    if (end == std::string_view::npos) {
      result.append(tmpl.substr(tag));
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
    result += "{{";
    result += trimmed;
    result += "}}";
    pos = end + 2;
  }
  return result;
}

[[nodiscard]] inline std::string transform_exists_sections(std::string_view tmpl) {
  std::string result;
  result.reserve(tmpl.size());
  std::size_t pos = 0;
  while (pos < tmpl.size()) {
    auto tag = tmpl.find("{{", pos);
    if (tag == std::string_view::npos) {
      result.append(tmpl.substr(pos));
      break;
    }
    result.append(tmpl.substr(pos, tag - pos));
    auto end = tmpl.find("}}", tag + 2);
    if (end == std::string_view::npos) {
      result.append(tmpl.substr(tag));
      break;
    }
    auto inner = tmpl.substr(tag + 2, end - tag - 2);
    auto trimmed = trim_sv(inner);
    if (trimmed == "#exists" || trimmed.starts_with("#exists ")) {
      auto rest = trimmed.substr(7);
      result += "{{#";
      if (!rest.empty()) {
        result += rest;
        result += " ";
      }
      result += "}}";
      pos = end + 2;
      continue;
    } else if (trimmed == "^exists" || trimmed.starts_with("^exists ")) {
      auto rest = trimmed.substr(8);
      result += "{{^";
      if (!rest.empty()) {
        result += rest;
        result += " ";
      }
      result += "}}";
      pos = end + 2;
      continue;
    } else {
      result.append(tmpl.substr(tag, end - tag + 2));
      pos = end + 2;
      continue;
    }
  }
  return result;
}

[[nodiscard]] inline std::string strip_bang_comments(std::string_view tmpl) {
  std::string result;
  result.reserve(tmpl.size());
  std::size_t pos = 0;
  while (pos < tmpl.size()) {
    auto cs = tmpl.find("{{!", pos);
    if (cs == std::string_view::npos) {
      result.append(tmpl.substr(pos));
      break;
    }
    if (cs > 0 && tmpl[cs - 1] == '{') {
      result.append(tmpl.substr(pos, cs - pos + 1));
      pos = cs + 1;
      continue;
    }
    result.append(tmpl.substr(pos, cs - pos));
    auto ce = tmpl.find("}}", cs + 3);
    if (ce == std::string_view::npos) {
      result += '{';
      pos = cs + 1;
    } else {
      pos = ce + 2;
    }
  }
  return result;
}

[[nodiscard]] inline std::string strip_comments(std::string_view tmpl) {
  std::string result;
  result.reserve(tmpl.size());
  std::size_t pos = 0;
  while (pos < tmpl.size()) {
    auto cs = tmpl.find("{#", pos);
    if (cs == std::string_view::npos) {
      result.append(tmpl.substr(pos));
      break;
    }
    if (cs > 0 && tmpl[cs - 1] == '{') {
      result.append(tmpl.substr(pos, cs - pos + 1));
      pos = cs + 1;
      continue;
    }
    result.append(tmpl.substr(pos, cs - pos));
    auto ce = tmpl.find("#}", cs + 2);
    if (ce == std::string_view::npos) {
      result += '{';
      pos = cs + 1;
    } else {
      pos = ce + 2;
    }
  }
  return result;
}

[[nodiscard]] constexpr std::string_view trim_tail_whitespace_for_lstrip(std::string_view sv) noexcept {
  auto nl = constexpr_rfind(sv, '\n');
  if (nl == std::string_view::npos) {
    for (auto c : sv) {
      if (c != ' ' && c != '\t') return sv;
    }
    return {};
  }
  for (std::size_t i = nl + 1; i < sv.size(); ++i) {
    if (sv[i] != ' ' && sv[i] != '\t') return sv;
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

}  // namespace injamm::detail
