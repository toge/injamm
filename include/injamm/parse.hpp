#pragma once

#include "bytecode.hpp"
#include "chunk.hpp"
#include "types.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
 * @return 対応する chunk_at_var::kind 列挙値
 * @details 該当しない場合は chunk_at_var::kind::root を返す。
 *          root は @prefix なしのプレースホルダーを表す。
 */
[[nodiscard]] constexpr chunk_at_var::kind parse_at_kind(std::string_view key) noexcept {
  if (key == "@last") {
    return chunk_at_var::kind::last;
  }
  if (key == "@first") {
    return chunk_at_var::kind::first;
  }
  if (key == "@index") {
    return chunk_at_var::kind::index;
  }
  if (key == "@key") {
    return chunk_at_var::kind::key;
  }
  return chunk_at_var::kind::root;
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
    auto fname = name.substr(0, paren);
    auto args_str = name.substr(paren + 1, name.size() - paren - 2);
    int arg1 = 0;
    int arg2 = 0;
    // カンマで分割して引数を解析
    auto comma = args_str.find(',');
    if (comma != std::string_view::npos) {
      // 2引数
      auto arg1_str = args_str.substr(0, comma);
      auto arg2_str = args_str.substr(comma + 1);
      for (auto c : arg1_str) {
        if (c >= '0' && c <= '9') arg1 = arg1 * 10 + (c - '0');
      }
      for (auto c : arg2_str) {
        if (c >= '0' && c <= '9') arg2 = arg2 * 10 + (c - '0');
      }
    } else {
      // 1引数
      for (auto c : args_str) {
        if (c >= '0' && c <= '9') arg1 = arg1 * 10 + (c - '0');
      }
    }
    if (fname == "left") return string_filter_entry{string_filter::left, arg1, 0};
    if (fname == "right") return string_filter_entry{string_filter::right, arg1, 0};
    if (fname == "center") return string_filter_entry{string_filter::center, arg1, 0};
    if (fname == "truncate") return string_filter_entry{string_filter::truncate, arg1, 0};
    if (fname == "substr") return string_filter_entry{string_filter::substr, arg1, arg2};
  }
  // 引数なしフィルタ
  if (name == "upper") return string_filter_entry{string_filter::upper, 0, 0};
  if (name == "lower") return string_filter_entry{string_filter::lower, 0, 0};
  if (name == "capitalize") return string_filter_entry{string_filter::capitalize, 0, 0};
  if (name == "title") return string_filter_entry{string_filter::title, 0, 0};
  if (name == "trim") return string_filter_entry{string_filter::trim, 0, 0};
  if (name == "ltrim") return string_filter_entry{string_filter::ltrim, 0, 0};
  if (name == "rtrim") return string_filter_entry{string_filter::rtrim, 0, 0};
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
    auto fname = name.substr(0, paren);
    auto arg_str = name.substr(paren + 1, name.size() - paren - 2);
    int arg = 0;
    for (auto c : arg_str) {
      if (c >= '0' && c <= '9') arg = arg * 10 + (c - '0');
    }
    if (fname == "mod") return int_filter_entry{int_filter::mod, arg};
    if (fname == "eq") return int_filter_entry{int_filter::eq, arg};
    if (fname == "zerofill") return int_filter_entry{int_filter::zerofill, arg};
  }
  // 引数なしフィルタ
  if (name == "abs")     return int_filter_entry{int_filter::abs, 0};
  if (name == "hex")     return int_filter_entry{int_filter::hex, 0};
  if (name == "oct")     return int_filter_entry{int_filter::oct, 0};
  if (name == "bin")     return int_filter_entry{int_filter::bin, 0};
  if (name == "neg")     return int_filter_entry{int_filter::neg, 0};
  if (name == "numify")  return int_filter_entry{int_filter::numify, 0};
  if (name == "is_neg")  return int_filter_entry{int_filter::is_neg, 0};
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
    auto fname = name.substr(0, paren);
    auto arg_str = name.substr(paren + 1, name.size() - paren - 2);
    int arg = 0;
    for (auto c : arg_str) {
      if (c >= '0' && c <= '9') arg = arg * 10 + (c - '0');
    }
    if (fname == "precision") return float_filter_entry{float_filter::precision, arg};
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
  std::size_t pos = 0;
  int if_depth = 0;
  int section_depth = 0;
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

/**
 * @brief 文字列を '|' で分割し、各パートの前後空白を除去する
 * @param input 入力文字列
 * @return 分割された文字列のベクター
 */
[[nodiscard]] constexpr std::vector<std::string_view> split_by_pipe(std::string_view input) {
  std::vector<std::string_view> result;
  std::size_t pos = 0;
  while (pos < input.size()) {
    auto pipe = input.find('|', pos);
    if (pipe == std::string_view::npos) {
      result.push_back(trim_sv(input.substr(pos)));
      break;
    }
    result.push_back(trim_sv(input.substr(pos, pipe - pos)));
    pos = pipe + 1;
  }
  return result;
}

/**
 * @brief テンプレート文字列をチャンク列にパースする（前方宣言）
 * @param tmpl テンプレート文字列
 * @return チャンクのベクター
 */
[[nodiscard]] constexpr auto parse(std::string_view tmpl) -> expected<std::vector<chunk>>;

/**
 * @brief コンテナにチャンクを出力するパーサー（内部実装）
 * @tparam Container push_back メソッドを持つ出力先コンテナ
 * @param result 出力先コンテナ
 * @param tmpl テンプレート文字列
 * @return 成功時は false、未知のフィルタがある場合は error_ctx を含む unexpected
 * @details Mustache 互換の構文を解析し、各種チャンク（リテラル、プレースホルダー、
 *          セクション、if、逆セクション、@変数）を生成する。
 *          constexpr 対応。文字列のコピーを避けるため string_view で処理する。
 */
template <class Container>
constexpr expected<bool> parse_into(Container& result, std::string_view tmpl) {
  std::size_t pos = 0;

  while (pos < tmpl.size()) {
    /** {{ タグの開始位置を検索 */
    auto tag_start = tmpl.find("{{", pos);
    if (tag_start == std::string_view::npos) {
      /** タグなし: 残り全てをリテラルチャンクとして出力 */
      result.push_back(chunk_literal{std::string{tmpl.substr(pos)}});
      break;
    }

    /** タグ開始前のリテラル部分を出力 */
    if (tag_start > pos) {
      result.push_back(chunk_literal{std::string{tmpl.substr(pos, tag_start - pos)}});
    }

    /** {{{ raw プレースホルダーのチェック */
    if (tag_start + 2 < tmpl.size() && tmpl[tag_start + 2] == '{') {
      auto end = tmpl.find("}}}", tag_start + 3);
      if (end == std::string_view::npos) {
        /** 閉じ {{{ がない場合はリテラルとして扱う */
        result.push_back(chunk_literal{std::string{tmpl.substr(tag_start, 1)}});
        pos = tag_start + 1;
        continue;
      }
      auto key = trim_sv(tmpl.substr(tag_start + 3, end - tag_start - 3));
      auto parts = split_by_pipe(key);
      auto actual_key = parts[0];
      std::vector<string_filter_entry> filters;
      std::vector<int_filter_entry> int_filters;
      std::vector<float_filter_entry> float_filters;
      for (std::size_t fi = 1; fi < parts.size(); ++fi) {
        auto sf = parse_string_filter(parts[fi]);
        if (sf) {
          filters.push_back(*sf);
          continue;
        }
        auto ifl = parse_int_filter(parts[fi]);
        if (ifl) {
          int_filters.push_back(*ifl);
          continue;
        }
        auto ffl = parse_float_filter(parts[fi]);
        if (ffl) {
          float_filters.push_back(*ffl);
          continue;
        }
        return std::unexpected(error_ctx{tag_start, error_code::unknown_filter, parts[fi]});
      }
      result.push_back(chunk_placeholder{std::string{actual_key}, true, std::move(filters), std::move(int_filters), std::move(float_filters)});
      pos = end + 3;
      continue;
    }

    /** {{ ... }} の内容を取得 */
    auto tag_end = tmpl.find("}}", tag_start + 2);
    if (tag_end == std::string_view::npos) {
      /** 閉じていない {{ はリテラルとして扱う */
      result.push_back(chunk_literal{std::string{tmpl.substr(tag_start, 1)}});
      pos = tag_start + 1;
      continue;
    }

    auto inner = trim_sv(tmpl.substr(tag_start + 2, tag_end - tag_start - 2));
    pos = tag_end + 2;

    /** 空タグはスキップ */
    if (inner.empty()) {
      continue;
    }

    /**
     * / で始まる閉じタグはトップレベルでは無視する
     * パーサーは parse_into を再帰的に呼び出してボディを処理するため、
     * 閉じタグの処理は各ブロックの検出時に行われる。
     */
    if (inner.starts_with("/")) {
      continue;
    }

    /** # で始まる: セクションまたは if */
    if (inner.starts_with("#")) {
      auto key = trim_sv(inner.substr(1));

      /** {{#if X}} — if ブロック */
      if (key.starts_with("if") && (key.size() == 2 || key[2] == ' ')) {
        auto expr_raw = key.size() > 2 ? trim_sv(key.substr(3)) : std::string_view{};

        /** フィルタチェーンの解析: "age | is_neg" → key="age", filters=[is_neg] */
        auto parts = split_by_pipe(expr_raw);
        auto expr = parts.empty() ? std::string_view{} : parts[0];
        std::vector<string_filter_entry> if_filters;
        std::vector<int_filter_entry> if_int_filters;
        std::vector<float_filter_entry> if_float_filters;
        for (std::size_t fi = 1; fi < parts.size(); ++fi) {
          auto sf = parse_string_filter(parts[fi]);
          if (sf) {
            if_filters.push_back(*sf);
            continue;
          }
          auto ifl = parse_int_filter(parts[fi]);
          if (ifl) {
            if_int_filters.push_back(*ifl);
            continue;
          }
          auto ffl = parse_float_filter(parts[fi]);
          if (ffl) {
            if_float_filters.push_back(*ffl);
            continue;
          }
          return std::unexpected(error_ctx{tag_start, error_code::unknown_filter, parts[fi]});
        }

        /**
         * {{/if}} を深さカウント付きで検索する
         * ネストされた {{#if}} も正しく扱うため、オープン/クローズの
         * バランスを depth で追跡する。
         */
        int depth = 1;
        std::size_t search_pos = pos;
        std::size_t close_pos = std::string_view::npos;

        while (search_pos < tmpl.size()) {
          auto next_open = tmpl.find("{{#if", search_pos);
          auto next_close = tmpl.find("{{/if}}", search_pos);
          if (next_close == std::string_view::npos) {
            break;
          }
          if (next_open != std::string_view::npos && next_open < next_close) {
            ++depth;
            search_pos = next_open + 5;
          } else {
            --depth;
            if (depth == 0) {
              close_pos = next_close;
              break;
            }
            search_pos = next_close + 7;
          }
        }

        std::string_view body;
        if (close_pos != std::string_view::npos) {
          body = tmpl.substr(pos, close_pos - pos);
          pos = close_pos + 7;
        } else {
          /** 閉じタグなし: 残り全てをボディとする */
          body = tmpl.substr(pos);
          pos = tmpl.size();
        }

        /** {{else}} でボディを then / else に分割 */
        auto else_pos = find_toplevel_else(body);
        std::string_view then_body, else_body;
        if (else_pos != std::string_view::npos) {
          then_body = body.substr(0, else_pos);
          auto else_tag_end = body.find("}}", else_pos + 2);
          else_body = (else_tag_end != std::string_view::npos) ? body.substr(else_tag_end + 2) : std::string_view{};
        } else {
          then_body = body;
          else_body = {};
        }

        chunk_if ci;
        ci.expr = std::string{expr};
        ci.filters = std::move(if_filters);
        ci.int_filters = std::move(if_int_filters);
        ci.float_filters = std::move(if_float_filters);
        {
          auto parsed = parse(then_body);
          if (!parsed) return std::unexpected(parsed.error());
          ci.then_branch = wrap_body_chunks(std::move(*parsed));
        }
        {
          auto parsed = parse(else_body);
          if (!parsed) return std::unexpected(parsed.error());
          ci.else_branch = wrap_body_chunks(std::move(*parsed));
        }
        result.push_back(std::move(ci));
        continue;
      }

      /** {{#@var}} — @var セクション */
      if (key.starts_with("@")) {
        auto var_kind = parse_at_kind(key);
        auto close_tag_str = std::string{"{{/"} + std::string{key} + "}}";
        auto body_start = pos;
        auto close_pos = tmpl.find(close_tag_str, pos);
        if (close_pos != std::string_view::npos) {
          auto body = tmpl.substr(body_start, close_pos - body_start);
          pos = close_pos + close_tag_str.size();
          chunk_at_section cas;
          cas.var = var_kind;
          {
            auto parsed = parse(body);
            if (!parsed) return std::unexpected(parsed.error());
            cas.body = wrap_body_chunks(std::move(*parsed));
          }
          cas.inverted = false;
          result.push_back(std::move(cas));
        }
        continue;
      }

      /** {{#key}} — 通常のセクション */
      {
        auto close_tag_str = std::string{"{{/"} + std::string{key} + "}}";
        auto open_tag_str = std::string{"{{#"} + std::string{key} + "}}";
        auto body_start = pos;

        /** ネストした同キーのセクションを考慮し深度カウントで閉じタグを検出 */
        int depth2 = 1;
        std::size_t search = pos;
        std::size_t close_pos = std::string_view::npos;

        while (search < tmpl.size()) {
          auto next_open = tmpl.find(open_tag_str, search);
          auto next_close = tmpl.find(close_tag_str, search);
          if (next_close == std::string_view::npos) {
            break;
          }
          if (next_open != std::string_view::npos && next_open < next_close) {
            ++depth2;
            search = next_open + open_tag_str.size();
          } else {
            --depth2;
            if (depth2 == 0) {
              close_pos = next_close;
              break;
            }
            search = next_close + close_tag_str.size();
          }
        }

        if (close_pos != std::string_view::npos) {
          auto body = tmpl.substr(body_start, close_pos - body_start);
          pos = close_pos + close_tag_str.size();
          {
            auto parsed = parse(body);
            if (!parsed) return std::unexpected(parsed.error());
            chunk_section cs;
            cs.key = std::string{key};
            cs.body = wrap_body_chunks(std::move(*parsed));
            result.push_back(std::move(cs));
          }
        } else {
          /** 閉じタグなし: ファイル末尾までをボディとする */
          auto body = tmpl.substr(body_start);
          pos = tmpl.size();
          {
            auto parsed = parse(body);
            if (!parsed) return std::unexpected(parsed.error());
            chunk_section cs;
            cs.key = std::string{key};
            cs.body = wrap_body_chunks(std::move(*parsed));
            result.push_back(std::move(cs));
          }
        }
      }
      continue;
    }

    /** ^ で始まる逆セクション */
    if (inner.starts_with("^")) {
      auto key = trim_sv(inner.substr(1));

      /** {{^@var}} — @var 逆セクション */
      if (key.starts_with("@")) {
        auto var_kind = parse_at_kind(key);
        auto close_tag_str = std::string{"{{/"} + std::string{key} + "}}";
        auto body_start = pos;
        auto close_pos = tmpl.find(close_tag_str, pos);
        if (close_pos != std::string_view::npos) {
          auto body = tmpl.substr(body_start, close_pos - body_start);
          pos = close_pos + close_tag_str.size();
          chunk_at_section cas;
          cas.var = var_kind;
          {
            auto parsed = parse(body);
            if (!parsed) return std::unexpected(parsed.error());
            cas.body = wrap_body_chunks(std::move(*parsed));
          }
          cas.inverted = true;
          result.push_back(std::move(cas));
        }
        continue;
      }

      /** {{^key}} — 通常の逆セクション */
      {
        auto close_tag_str = std::string{"{{/"} + std::string{key} + "}}";
        auto open_tag_str = std::string{"{{^"} + std::string{key} + "}}";
        auto body_start = pos;

        int depth2 = 1;
        std::size_t search = pos;
        std::size_t close_pos = std::string_view::npos;

        while (search < tmpl.size()) {
          auto next_open = tmpl.find(open_tag_str, search);
          auto next_close = tmpl.find(close_tag_str, search);
          if (next_close == std::string_view::npos) {
            break;
          }
          if (next_open != std::string_view::npos && next_open < next_close) {
            ++depth2;
            search = next_open + open_tag_str.size();
          } else {
            --depth2;
            if (depth2 == 0) {
              close_pos = next_close;
              break;
            }
            search = next_close + close_tag_str.size();
          }
        }

        std::string_view body;
        if (close_pos != std::string_view::npos) {
          body = tmpl.substr(body_start, close_pos - body_start);
          pos = close_pos + close_tag_str.size();
        } else {
          body = tmpl.substr(body_start);
          pos = tmpl.size();
        }
        {
          auto parsed = parse(body);
          if (!parsed) return std::unexpected(parsed.error());
          chunk_inverted ci;
          ci.key = std::string{key};
          ci.body = wrap_body_chunks(std::move(*parsed));
          result.push_back(std::move(ci));
        }
      }
      continue;
    }

    /** @root.field はプレースホルダとして扱う（パス情報を保持） */
    if (inner.starts_with("@root.")) {
      result.push_back(chunk_placeholder{std::string{inner}, false});
      continue;
    }

    /** @ で始まる @vars（@index / @first / @last / @root） */
    if (inner.starts_with("@")) {
      result.push_back(chunk_at_var{parse_at_kind(inner)});
      continue;
    }

    /** 通常のプレースホルダー（フィルタ対応） */
    auto parts = split_by_pipe(inner);
    auto filter_key = parts[0];
    std::vector<string_filter_entry> filters;
    std::vector<int_filter_entry> int_filters;
    std::vector<float_filter_entry> float_filters;
    for (std::size_t fi = 1; fi < parts.size(); ++fi) {
      auto sf = parse_string_filter(parts[fi]);
      if (sf) {
        filters.push_back(*sf);
        continue;
      }
      auto ifl = parse_int_filter(parts[fi]);
      if (ifl) {
        int_filters.push_back(*ifl);
        continue;
      }
      auto ffl = parse_float_filter(parts[fi]);
      if (ffl) {
        float_filters.push_back(*ffl);
        continue;
      }
      return std::unexpected(error_ctx{tag_start, error_code::unknown_filter, parts[fi]});
    }
    result.push_back(chunk_placeholder{std::string{filter_key}, false, std::move(filters), std::move(int_filters), std::move(float_filters)});
  }
  return false;
}

/**
 * @brief テンプレート文字列をチャンク列にパースする
 * @param tmpl テンプレート文字列
 * @return 正常時はチャンクのベクター、エラー時は error_ctx
 * @details parse_into を呼び出し、結果を expected で返す。
 *          未知のフィルタ名は error_code::unknown_filter として報告される。
 */
[[nodiscard]] constexpr auto parse(std::string_view tmpl) -> expected<std::vector<chunk>> {
  std::vector<chunk> result;
  auto r = parse_into(result, tmpl);
  if (!r) {
    return std::unexpected(r.error());
  }
  return result;
}

constexpr int max_var_depth = 100;

template <class ConstMap>
[[nodiscard]] expected<std::string> expand_var_refs(std::string_view content, ConstMap const& consts, int depth = 0) {
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
    auto paren_end = content.find(')', paren_start);
    if (paren_end == std::string_view::npos) {
      result.append(content.substr(var_start));
      break;
    }

    auto name = content.substr(paren_start, paren_end - paren_start);
    auto it = consts.find(name);
    if (it == consts.end()) {
      return std::unexpected(error_ctx{var_start, error_code::unknown_key, "undefined @var constant"});
    }

    auto expanded = expand_var_refs(std::string_view{it->second}, consts, depth + 1);
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
      auto expanded = expand_var_refs(raw_inner, consts);
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

    auto inner = tmpl.substr(tag_start + 2, tag_end - tag_start - 2);
    auto expanded = expand_var_refs(inner, consts);
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

} // namespace injamm::detail
