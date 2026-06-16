#pragma once

#include "types.hpp"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace injamm::detail {

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
 * @brief @変数の種別を判定する
 * @param key "@last" / "@first" / "@index" のいずれか
 * @return 対応する at_var_kind 列挙値
 * @details 該当しない場合は at_var_kind::root を返す。
 *          root は @prefix なしのプレースホルダーを表す。
 */
[[nodiscard]] constexpr at_var_kind parse_at_kind(std::string_view key) noexcept {
  if (key == "@last") {
    return at_var_kind::last;
  }
  if (key == "@first") {
    return at_var_kind::first;
  }
  if (key == "@index") {
    return at_var_kind::index;
  }
  if (key == "@key") {
    return at_var_kind::key;
  }
  return at_var_kind::root;
}

/**
 * @brief フィルタ名文字列を string_filter 列挙値に変換する
 * @param name フィルタ名（"to_upper" / "to_lower" / "trim"）
 * @return 対応する string_filter 列挙値、未知の場合は std::nullopt
 */
[[nodiscard]] constexpr std::optional<string_filter_entry> parse_string_filter(std::string_view name) noexcept {
  // 引数付きフィルタの処理
  auto paren = name.find('(');
  if (paren != std::string_view::npos && name.back() == ')') {
    auto fname    = name.substr(0, paren);
    auto args_str = name.substr(paren + 1, name.size() - paren - 2);
    int  arg1     = 0;
    int  arg2     = 0;
    // カンマで分割して引数を解析
    auto comma = args_str.find(',');
    if (comma != std::string_view::npos) {
      // 2引数
      auto arg1_str = args_str.substr(0, comma);
      auto arg2_str = args_str.substr(comma + 1);
      for (auto c : arg1_str) {
        if (c >= '0' && c <= '9')
          arg1 = arg1 * 10 + (c - '0');
      }
      for (auto c : arg2_str) {
        if (c >= '0' && c <= '9')
          arg2 = arg2 * 10 + (c - '0');
      }
    } else {
      // 1引数
      for (auto c : args_str) {
        if (c >= '0' && c <= '9')
          arg1 = arg1 * 10 + (c - '0');
      }
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
  return std::nullopt;
}

/**
 * @brief フィルタ名文字列を int_filter 列挙値に変換する
 * @param name フィルタ名（"abs" / "hex" / "oct" / "bin"）
 * @return 対応する int_filter 列挙値、未知の場合は std::nullopt
 */
[[nodiscard]] constexpr std::optional<int_filter_entry> parse_int_filter(std::string_view name) noexcept {
  // 引数付きフィルタの処理
  auto paren = name.find('(');
  if (paren != std::string_view::npos && name.back() == ')') {
    auto fname   = name.substr(0, paren);
    auto arg_str = name.substr(paren + 1, name.size() - paren - 2);
    int  arg     = 0;
    for (auto c : arg_str) {
      if (c >= '0' && c <= '9')
        arg = arg * 10 + (c - '0');
    }
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
  auto paren = name.find('(');
  if (paren != std::string_view::npos && name.back() == ')') {
    auto fname   = name.substr(0, paren);
    auto arg_str = name.substr(paren + 1, name.size() - paren - 2);
    int  arg     = 0;
    for (auto c : arg_str) {
      if (c >= '0' && c <= '9')
        arg = arg * 10 + (c - '0');
    }
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
    auto tag_pos = body.find("{{", pos);
    if (tag_pos == std::string_view::npos) {
      break;
    }
    auto end = body.find("}}", tag_pos + 2);
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
  static constexpr std::size_t            max_parts = 8;
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
    auto pipe = input.find('|', pos);
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

}  // namespace injamm::detail
