#pragma once

#include "chunk.hpp"
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
  return chunk_at_var::kind::root;
}

/**
 * @brief if ボディ内のトップレベル {{else}} の位置を検索する
 * @param body if ボディ文字列
 * @return {{else}} の開始位置、見つからない場合は std::string_view::npos
 * @details ネストされたセクション（{{#...}}）や if（{{#if...}}）内の else は
 *          深度カウントにより無視する。{{/if}} または {{/section}} で深度が
 *          減少する。深度 0 でのみ {{else}} をトップレベルと判定する。
 */
[[nodiscard]] constexpr std::size_t find_toplevel_else(std::string_view body) noexcept {
  std::size_t pos = 0;
  int depth = 0;
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
      ++depth;
    } else if (tag_inner.starts_with("#") && !tag_inner.starts_with("#if")) {
      ++depth;
    } else if (tag_inner.starts_with("/")) {
      if (depth > 0) {
        --depth;
      }
    } else if (tag_inner == "else" && depth == 0) {
      return tag_pos;
    }
    pos = end + 2;
  }
  return std::string_view::npos;
}

/**
 * @brief テンプレート文字列をチャンク列にパースする（前方宣言）
 * @param tmpl テンプレート文字列
 * @return チャンクのベクター
 */
[[nodiscard]] constexpr auto parse(std::string_view tmpl) -> std::vector<chunk>;

/**
 * @brief コンテナにチャンクを出力するパーサー（内部実装）
 * @tparam Container push_back メソッドを持つ出力先コンテナ
 * @param result 出力先コンテナ
 * @param tmpl テンプレート文字列
 * @details Mustache 互換の構文を解析し、各種チャンク（リテラル、プレースホルダー、
 *          セクション、if、逆セクション、@変数）を生成する。
 *          constexpr 対応。文字列のコピーを避けるため string_view で処理する。
 */
template <class Container>
constexpr void parse_into(Container& result, std::string_view tmpl) {
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
      result.push_back(chunk_placeholder{std::string{key}, true});
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
        auto expr = key.size() > 2 ? trim_sv(key.substr(3)) : std::string_view{};

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
        ci.then_branch = wrap_body_chunks(parse(then_body));
        ci.else_branch = wrap_body_chunks(parse(else_body));
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
          cas.body = wrap_body_chunks(parse(body));
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
          chunk_section cs;
          cs.key = std::string{key};
          cs.body = wrap_body_chunks(parse(body));
          result.push_back(std::move(cs));
        } else {
          /** 閉じタグなし: ファイル末尾までをボディとする */
          auto body = tmpl.substr(body_start);
          pos = tmpl.size();
          chunk_section cs;
          cs.key = std::string{key};
          cs.body = wrap_body_chunks(parse(body));
          result.push_back(std::move(cs));
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
          cas.body = wrap_body_chunks(parse(body));
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
        chunk_inverted ci;
        ci.key = std::string{key};
        ci.body = wrap_body_chunks(parse(body));
        result.push_back(std::move(ci));
      }
      continue;
    }

    /** @ で始まる @vars（@index / @first / @last） */
    if (inner.starts_with("@")) {
      result.push_back(chunk_at_var{parse_at_kind(inner)});
      continue;
    }

    /** 通常のプレースホルダー */
    result.push_back(chunk_placeholder{std::string{inner}, false});
  }
}

/**
 * @brief テンプレート文字列をチャンク列にパースする
 * @param tmpl テンプレート文字列
 * @return チャンクのベクター
 * @details parse_into を呼び出し、結果を std::vector<chunk> で返す。
 *          各チャンクはレンダリング時にバイトコードまたは直接実行される。
 */
[[nodiscard]] constexpr auto parse(std::string_view tmpl) -> std::vector<chunk> {
  std::vector<chunk> result;
  parse_into(result, tmpl);
  return result;
}

} // namespace injamm::detail
