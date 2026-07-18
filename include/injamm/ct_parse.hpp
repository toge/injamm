#pragma once

#include "ct_chunk.hpp"
#include "parse.hpp"
#include <cstddef>
#include <span>
#include <string_view>

namespace injamm::detail {

/**
 * @brief コンパイル時パーサーのコンテキスト（SoA 形式）
 * @details テンプレートのパース中に、チャンクの種類・テキスト・フラグ・本体範囲などを
 *          ct_parsed_template に逐次追加するための補助構造体。
 *          SoA（Structure of Arrays）形式で各チャンク情報を保持する。
 * @tparam MaxChunks 保持可能な最大チャンク数
 */
template <std::size_t MaxChunks>
struct ct_parse_context {
  /** @brief パース結果を格納するテンプレート */
  ct_parsed_template<MaxChunks> tmpl{};

  /**
   * @brief リテラルチャンクを追加する
   * @param text リテラル文字列
   * @return 追加されたチャンクのインデックス
   */
  constexpr std::size_t push_literal(std::string_view text) {
    auto idx = tmpl.size;
    tmpl.push_literal(text);
    return idx;
  }

  /**
   * @brief プレースホルダチャンクを追加する
   * @param key 変数キー（例: "name"）
   * @param raw 生出力モード（{{{var}}} の場合に true）
   * @param filter_list 適用するフィルタのリスト
   * @return 追加されたチャンクのインデックス
   */
  constexpr std::size_t
  push_placeholder(std::string_view key, bool raw,
                   std::span<string_filter_entry const> filter_list = {},
                   std::span<int_filter_entry const> int_filter_list = {},
                   std::span<float_filter_entry const> float_filter_list = {}) {
    auto idx = tmpl.size;
    tmpl.push_placeholder(key, raw, filter_list, int_filter_list, float_filter_list);
    return idx;
  }

  /**
   * @brief セクションチャンクを追加する
   * @param key        セクションキー
   * @param body_start 本体の開始チャンクインデックス
   * @param body_end   本体の終了チャンクインデックス
   * @return 追加されたチャンクのインデックス
   */
  constexpr std::size_t push_section(std::string_view key, std::size_t body_start, std::size_t body_end) {
    auto idx = tmpl.size;
    tmpl.push_section(key, body_start, body_end);
    return idx;
  }

  /**
   * @brief 逆セクションチャンクを追加する
   * @param key        セクションキー
   * @param body_start 本体の開始チャンクインデックス
   * @param body_end   本体の終了チャンクインデックス
   * @return 追加されたチャンクのインデックス
   */
  constexpr std::size_t push_inverted(std::string_view key, std::size_t body_start, std::size_t body_end) {
    auto idx = tmpl.size;
    tmpl.push_inverted(key, body_start, body_end);
    return idx;
  }

  /**
   * @brief @var チャンクを追加する
   * @param var @index / @first / @last / @root の種別
   * @return 追加されたチャンクのインデックス
   */
  constexpr std::size_t push_at_var(at_var_kind var) {
    auto idx = tmpl.size;
    tmpl.push_at_var(var);
    return idx;
  }

  /**
   * @brief @var セクションチャンクを追加する
   * @param var         @index / @first / @last / @root の種別
   * @param body_start 本体の開始チャンクインデックス
   * @param body_end   本体の終了チャンクインデックス
   * @param inverted   逆セクションの場合に true
   * @return 追加されたチャンクのインデックス
   */
  constexpr std::size_t push_at_section(at_var_kind var, std::size_t body_start, std::size_t body_end,
                                         bool inverted) {
    auto idx = tmpl.size;
    tmpl.push_at_section(var, body_start, body_end, inverted);
    return idx;
  }

  /**
   * @brief if/else チャンクを追加する
   * @param expr        条件式
   * @param then_start  then 節の開始チャンクインデックス
   * @param then_end    then 節の終了チャンクインデックス
   * @param else_start  else 節の開始チャンクインデックス
   * @param else_end    else 節の終了チャンクインデックス
   * @param filter_list 文字列フィルタのリスト
   * @param int_filter_list 整数フィルタのリスト
   * @param float_filter_list 実数フィルタのリスト
   * @return 追加されたチャンクのインデックス
   */
  constexpr std::size_t
  push_if(std::string_view expr, std::size_t then_start, std::size_t then_end,
          std::size_t else_start, std::size_t else_end,
          std::span<string_filter_entry const> filter_list = {},
          std::span<int_filter_entry const> int_filter_list = {},
          std::span<float_filter_entry const> float_filter_list = {}) {
    auto idx = tmpl.size;
    tmpl.push_if(expr, then_start, then_end, else_start, else_end,
                 filter_list, int_filter_list, float_filter_list);
    return idx;
  }

  /**
   * @brief 既存の if チャンクの本体範囲を後から更新する
   * @param idx         更新対象のチャンクインデックス
   * @param then_start  then 節の開始チャンクインデックス
   * @param then_end    then 節の終了チャンクインデックス
   * @param else_start  else 節の開始チャンクインデックス
   * @param else_end    else 節の終了チャンクインデックス
   */
  constexpr void update_if(std::size_t idx, std::size_t then_start, std::size_t then_end,
                            std::size_t else_start, std::size_t else_end) {
    tmpl.body_starts[idx] = then_start;
    tmpl.body_ends[idx] = then_end;
    tmpl.else_starts[idx] = else_start;
    tmpl.else_ends[idx] = else_end;
  }

  /**
   * @brief 既存のセクションチャンクの本体範囲を後から更新する
   * @param idx         更新対象のチャンクインデックス
   * @param body_start  本体の開始チャンクインデックス
   * @param body_end    本体の終了チャンクインデックス
   * @param else_start  else 節の開始チャンクインデックス（省略時は 0）
   * @param else_end    else 節の終了チャンクインデックス（省略時は 0）
   */
  constexpr void update_section(std::size_t idx, std::size_t body_start, std::size_t body_end,
                                 std::size_t else_start = 0, std::size_t else_end = 0) {
    tmpl.body_starts[idx] = body_start;
    tmpl.body_ends[idx] = body_end;
    tmpl.else_starts[idx] = else_start;
    tmpl.else_ends[idx] = else_end;
  }

  /**
   * @brief 既存の @var セクションチャンクの本体範囲を後から更新する
   * @param idx         更新対象のチャンクインデックス
   * @param body_start  本体の開始チャンクインデックス
   * @param body_end    本体の終了チャンクインデックス
   */
  constexpr void update_at_section(std::size_t idx, std::size_t body_start, std::size_t body_end) {
    tmpl.body_starts[idx] = body_start;
    tmpl.body_ends[idx] = body_end;
  }

  /**
   * @brief break チャンクを追加する
   */
  constexpr void push_break() {
    tmpl.push_break();
  }

  /**
   * @brief continue チャンクを追加する
   */
  constexpr void push_continue() {
    tmpl.push_continue();
  }

  /**
   * @brief partial 参照チャンクを追加する
   * @param partial_index 事前スキャンされた partial 定義のインデックス
   * @param name          partial 名
   * @return 追加されたチャンクのインデックス
   */
  constexpr std::size_t push_partial_ref(std::size_t partial_index, std::string_view name) {
    auto idx = tmpl.size;
    tmpl.push_partial_ref(partial_index, name);
    return idx;
  }
};

/**
 * @brief テンプレート文字列をコンパイル時にパースし、SoA 形式のチャンク列に出力する
 * @details 入力テンプレート内の `{{...}}` タグを検出しながら順次パースし、
 *          ct_parse_context を介してチャンクを追加していく。
 *          対応タグ:
 *          - `{{var}}` / `{{{var}}}`: プレースホルダ
 *          - `{{#section}}...{{/section}}`: セクション
 *          - `{{^section}}...{{/section}}`: 逆セクション
 *          - `{{#if X}}...{{else}}...{{/if}}`: 条件分岐
 *          - `{{#@var}}...{{/@var}}`: @var セクション
 *          - `{{^@var}}...{{/@var}}`: @var 逆セクション
 *          - `{{@index}}` / `{{@first}}` / `{{@last}}` / `{{@root}}`: @var
 * @tparam MaxChunks 保持可能な最大チャンク数
 * @param[out] ctx  パース結果を書き込むコンテキスト
 * @param[in]  tmpl 入力テンプレート文字列
 */
template <std::size_t MaxChunks>
constexpr void ct_parse_into(ct_parse_context<MaxChunks>& ctx, std::string_view tmpl,
                              bool trim_blocks = false, bool lstrip_blocks = false) {
  /** @brief 現在のパース位置（バイトオフセット） */
  std::size_t pos = 0;

  // -- メインループ: テンプレート全体を走査しながらタグを解析する --
  while (pos < tmpl.size()) {
    /** @brief {# ... #} コメントをスキップ（{{# はセクション/if タグなので除外） */
    auto comment_start = constexpr_find(tmpl, "{#", pos);
    while (comment_start != std::string_view::npos && comment_start > 0 && tmpl[comment_start - 1] == '{') {
      comment_start = constexpr_find(tmpl, "{#", comment_start + 2);
    }
    auto tag_start = constexpr_find(tmpl, "{{", pos);
    if (comment_start != std::string_view::npos && (tag_start == std::string_view::npos || comment_start < tag_start)) {
      if (comment_start > pos)
        ctx.push_literal(tmpl.substr(pos, comment_start - pos));
      auto comment_end = constexpr_find(tmpl, "#}", comment_start + 2);
      if (comment_end != std::string_view::npos) {
        pos = comment_end + 2;
      } else {
        ctx.push_literal(tmpl.substr(comment_start, 1));
        pos = comment_start + 1;
      }
      continue;
    }

    /** @brief `{{` が見つからない場合は、残り全体をリテラルとして追加して終了 */
    if (tag_start == std::string_view::npos) {
      ctx.push_literal(tmpl.substr(pos));
      break;
    }

    /** @brief `{{` より前にリテラル文字列があれば追加 */
    if (tag_start > pos) {
      auto literal = tmpl.substr(pos, tag_start - pos);
      if (lstrip_blocks && is_block_tag_start(tmpl, tag_start)) {
        literal = trim_tail_whitespace_for_lstrip(literal);
      }
      if (!literal.empty()) {
        ctx.push_literal(literal);
      }
    }

    // -- {{{ raw プレースホルダの処理 --
    /**
     * @brief 3連続の `{{{` は raw 出力（HTML エスケープなし）のプレースホルダ
     * @details `{{{var}}}` のように記述すると、値が HTML エスケープされずにそのまま出力される。
     *          ステンシルモード用。
     */
    if (tag_start + 2 < tmpl.size() && tmpl[tag_start + 2] == '{') {
      /** @brief `}}}` 終了タグを検索 */
      auto end = constexpr_find(tmpl, "}}}", tag_start + 3);
      if (end == std::string_view::npos) {
        /* 閉じタグがない場合は `{` をリテラルとして扱い 1 バイト進める */
        ctx.push_literal(tmpl.substr(tag_start, 1));
        pos = tag_start + 1;
        continue;
      }
      /** @brief {{{ と }}} の間をキーとして raw プレースホルダを追加 */
      auto raw_key = trim_sv(tmpl.substr(tag_start + 3, end - tag_start - 3));
      auto parts = split_by_pipe(raw_key);
      auto actual_key = parts[0];
      std::vector<string_filter_entry> filter_list;
      std::vector<int_filter_entry> int_filter_list;
      std::vector<float_filter_entry> float_filter_list;
      for (std::size_t fi = 1; fi < parts.size(); ++fi) {
        auto sf = parse_string_filter(parts[fi]);
        if (sf) {
          filter_list.push_back(*sf);
          continue;
        }
        auto ifl = parse_int_filter(parts[fi]);
        if (ifl) {
          int_filter_list.push_back(*ifl);
          continue;
        }
        auto ffl = parse_float_filter(parts[fi]);
        if (ffl) {
          float_filter_list.push_back(*ffl);
          continue;
        }
      }
      ctx.push_placeholder(actual_key, true, filter_list, int_filter_list, float_filter_list);
      pos = end + 3;
      if (trim_blocks && pos < tmpl.size() && tmpl[pos] == '\n') ++pos;
      continue;
    }

    // -- {{! comment }} をスキップ（ネストした {{ }} を考慮） --
    if (tag_start + 2 < tmpl.size() && tmpl[tag_start + 2] == '!') {
      std::size_t comment_pos = tag_start + 3;
      bool comment_closed = false;
      while (comment_pos < tmpl.size()) {
        auto next_dbl_open  = constexpr_find(tmpl, "{{", comment_pos);
        auto next_dbl_close = constexpr_find(tmpl, "}}", comment_pos);
        if (next_dbl_close == std::string_view::npos) break;
        if (next_dbl_open != std::string_view::npos && next_dbl_open < next_dbl_close) {
          auto inner_close = constexpr_find(tmpl, "}}", next_dbl_open + 2);
          if (inner_close == std::string_view::npos) break;
          comment_pos = inner_close + 2;
        } else {
          pos = next_dbl_close + 2;
          if (trim_blocks && pos < tmpl.size() && tmpl[pos] == '\n') ++pos;
          comment_closed = true;
          break;
        }
      }
      if (!comment_closed) {
        ctx.push_literal(tmpl.substr(tag_start, 1));
        pos = tag_start + 1;
      }
      continue;
    }

    // -- `}}` 終了タグの検索 --
    /** @brief 対応する `}}` の位置 */
    auto tag_end = constexpr_find(tmpl, "}}", tag_start + 2);
    if (tag_end == std::string_view::npos) {
      /* 閉じタグがない場合は `{` をリテラルとして扱い 1 バイト進める */
      ctx.push_literal(tmpl.substr(tag_start, 1));
      pos = tag_start + 1;
      continue;
    }

    /** @brief `{{` と `}}` の間の内容（前後の空白除去済み、tilde 除去） */
    auto inner_raw = tmpl.substr(tag_start + 2, tag_end - tag_start - 2);
    bool leading_tilde = inner_raw.starts_with("~");
    bool trailing_tilde = inner_raw.ends_with("~") && inner_raw.size() > 1;
    auto inner = trim_sv(inner_raw);
    if (leading_tilde && inner.size() > 1 && inner.front() == '~')
      inner = trim_sv(inner.substr(1));
    if (trailing_tilde && inner.size() > 1 && inner.back() == '~')
      inner = trim_sv(inner.substr(0, inner.size() - 1));
    pos = tag_end + 2;
    if (trim_blocks && pos < tmpl.size() && tmpl[pos] == '\n') ++pos;

    /** @brief 空タグはスキップ */
    if (inner.empty()) {
      continue;
    }

    /**
     * @brief `/` で始まるタグ（閉じタグ）はスキップ
     * @details 閉じタグは上位の再帰呼び出しですでに処理済みであるため、
     *          ここでは無視する。
     */
    if (inner.starts_with("/")) {
      continue;
    }

    // -- `>` で始まるタグ: 外部レジストリからの partial インクルード --
    if (inner.starts_with(">")) {
      auto partial_name = trim_sv(inner.substr(1));
      std::size_t pidx = 0;
      bool found = false;
      for (; pidx < ctx.tmpl.partial_total; ++pidx) {
        if (ctx.tmpl.partial_names[pidx] == partial_name) {
          found = true;
          break;
        }
      }
      if (!found) {
        ctx.tmpl.partial_names[ctx.tmpl.partial_total] = partial_name;
        pidx = ctx.tmpl.partial_total;
        ++ctx.tmpl.partial_total;
      }
      ctx.push_partial_ref(pidx, partial_name);
      continue;
    }

    // -- `#` で始まるタグ: セクションまたは if --
    if (inner.starts_with("#")) {
      /** @brief `#` 以降のキー部分 */
      auto key = trim_sv(inner.substr(1));

      if (key == "break") {
        ctx.push_break();
        continue;
      }

      if (key == "continue") {
        ctx.push_continue();
        continue;
      }

      // -- {{#if X}} の処理 --
      /**
       * @brief if 条件分岐タグの解析
       * @details `{{#if X}}...{{else}}...{{/if}}` の形式を解析する。
       *          条件式 X が真と評価される場合は then 節、偽の場合は else 節が描画される。
       *          else 節は省略可能。
       */
      // -- {{#partialdef name}} をスキップ（事前スキャン済み） --
      if (key.starts_with("partialdef ")) {
        auto close_tag = constexpr_find(tmpl, "{{/partialdef}}", pos);
        if (close_tag != std::string_view::npos) {
          pos = close_tag + 15;
          if (trim_blocks && pos < tmpl.size() && tmpl[pos] == '\n') ++pos;
        }
        continue;
      }

      // -- {{#partial name}} を解決 --
      if (key.starts_with("partial ")) {
        auto partial_name = trim_sv(key.substr(8));
        std::size_t pidx = 0;
        bool found = false;
        for (; pidx < ctx.tmpl.partial_total; ++pidx) {
          if (ctx.tmpl.partial_names[pidx] == partial_name) {
            found = true;
            break;
          }
        }
        ctx.push_partial_ref(found ? pidx : SIZE_MAX, partial_name);
        continue;
      }

      if (key.starts_with("if") && (key.size() == 2 || key[2] == ' ')) {
        /** @brief if の条件式 */
        auto expr_raw = key.size() > 2 ? trim_sv(key.substr(3)) : std::string_view{};

        /** フィルタチェーンの解析 */
        auto parts = split_by_pipe(expr_raw);
        auto expr = parts.empty() ? std::string_view{} : parts[0];
        bool negate_if = false;
        auto check_expr = expr;
        if (check_expr.starts_with("!")) {
          negate_if = true;
          check_expr = trim_sv(check_expr.substr(1));
        }
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
        }

        /** @brief ネスト対応のための深さカウンタ */
        int depth = 1;
        std::size_t search_pos = pos;
        std::size_t close_pos = std::string_view::npos;

        /**
         * @brief {{/if}} までの間でネストした {{#if}} を考慮して閉じ位置を決定
         * @details depth が 0 になるまで閉じタグを検索し続ける。
         *          開きタグに出会うたびに depth を増やし、閉じタグに出会うたびに減らす。
         */
        while (search_pos < tmpl.size()) {
          auto next_open = constexpr_find(tmpl, "{{#if", search_pos);
          auto next_close = constexpr_find(tmpl, "{{/if}}", search_pos);
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

        /** @brief if 本体の生文字列 */
        std::string_view body;
        if (close_pos != std::string_view::npos) {
          body = tmpl.substr(pos, close_pos - pos);
          if (lstrip_blocks) body = trim_tail_whitespace_for_lstrip(body);
          pos = close_pos + 7;
          if (trim_blocks && pos < tmpl.size() && tmpl[pos] == '\n') ++pos;
        } else {
          body = tmpl.substr(pos);
          pos = tmpl.size();
        }

        /**
         * @brief トップレベルの {{else}} を検出して then / else に分割
         * @details find_toplevel_else はネストを考慮して、最上位の else のみを検出する。
         */
        auto else_pos = find_toplevel_else(body);
        std::string_view then_body, else_body;
        if (else_pos != std::string_view::npos) {
          then_body = body.substr(0, else_pos);
          if (lstrip_blocks) then_body = trim_tail_whitespace_for_lstrip(then_body);
          auto else_tag_end = constexpr_find(body, "}}", else_pos + 2);
          else_body = (else_tag_end != std::string_view::npos) ? body.substr(else_tag_end + 2) : std::string_view{};
          if (lstrip_blocks) else_body = trim_tail_whitespace_for_lstrip(else_body);
        } else {
          then_body = body;
          else_body = {};
        }

        /** @brief if チャンクを仮追加（範囲は後で更新） */
        auto chunk_idx = ctx.push_if(expr, 0, 0, 0, 0,
                                         if_filters, if_int_filters, if_float_filters);

        /** 定数条件の最適化: リテラル整数はコンパイル時に真偽判定し、到達不可能な分岐のパースを省略 */
        bool constant_folded = false;
        if (if_filters.empty() && if_int_filters.empty() && if_float_filters.empty()) {
          if (auto int_val = parse_int_literal(check_expr)) {
            constant_folded = true;
            bool cond = negate_if ? (*int_val == 0) : (*int_val != 0);
            if (cond) {
              auto then_start = ctx.tmpl.size;
              ct_parse_into(ctx, then_body, trim_blocks, lstrip_blocks);
              auto then_end = ctx.tmpl.size;
              ctx.update_if(chunk_idx, then_start, then_end, then_end, then_end);
            } else {
              auto else_start = ctx.tmpl.size;
              ct_parse_into(ctx, else_body, trim_blocks, lstrip_blocks);
              auto else_end = ctx.tmpl.size;
              ctx.update_if(chunk_idx, else_start, else_start, else_start, else_end);
            }
          }
        }

        if (!constant_folded) {
          /** @brief then 節を再帰パース */
          auto then_start = ctx.tmpl.size;
          ct_parse_into(ctx, then_body, trim_blocks, lstrip_blocks);
          auto then_end = ctx.tmpl.size;

          /** @brief else 節を再帰パース */
          auto else_start = ctx.tmpl.size;
          ct_parse_into(ctx, else_body, trim_blocks, lstrip_blocks);
          auto else_end = ctx.tmpl.size;

          /** @brief 仮追加したチャンクの範囲を更新（check_expr で ! を除去） */
          ctx.update_if(chunk_idx, then_start, then_end, else_start, else_end);
          /** @brief テキストを check_expr（!除去済み）に置換し、否定フラグを設定 */
          ctx.tmpl.texts[chunk_idx] = check_expr;
          if (negate_if) ctx.tmpl.flags[chunk_idx] |= 0x80;
        }
        continue;
      }

      // -- {{#loop.X}} セクションの処理 --
      /**
       * @brief loop.* 条件セクションの解析
       * @details `{{#loop.is_first}}...{{/loop.is_first}}` のように記述し、
       *          ループの先頭要素などの条件に応じて本体が描画される。
       */
      if (auto k = parse_loop_kind(key); k) {
        at_var_kind var_kind = *k;
        /** @brief 対応する閉じタグ位置（例: "{{/loop.is_first}}"） */
        auto body_start = pos;
        auto close_pos = constexpr_find_tag(tmpl, "{{/", key, "}}", pos);
        if (close_pos != std::string_view::npos) {
          auto body = tmpl.substr(body_start, close_pos - body_start);
          if (lstrip_blocks) body = trim_tail_whitespace_for_lstrip(body);
          pos = close_pos + 5 + key.size();
          if (trim_blocks && pos < tmpl.size() && tmpl[pos] == '\n') ++pos;

          /** @brief loop.* セクションチャンクを追加し、本体を再帰パース */
          auto chunk_idx = ctx.push_at_section(var_kind, 0, 0, false);
          auto body_start_idx = ctx.tmpl.size;
          ct_parse_into(ctx, body, trim_blocks, lstrip_blocks);
          auto body_end_idx = ctx.tmpl.size;

          ctx.update_at_section(chunk_idx, body_start_idx, body_end_idx);
        }
        continue;
      }

      // -- {{#exists var}} → {{#var}} --
      /** @brief close tag 探索用に exists 前置詞を保持したキー */
      auto close_key = key;
      if (key.starts_with("exists ") || key.starts_with("exists\t")) {
        if (key.size() == 7) continue;
        close_key = key; // keep "exists name" for close tag search
        key = trim_sv(key.substr(7));
        if (key.empty()) continue;
      }

      // -- {{#key}} 通常セクションの処理 --
      /**
       * @brief 通常セクションの解析
       * @details `{{#key}}...{{/key}}` の形式を解析する。
       *          同一キーでのネストを考慮して depth カウントを行う。
       *          セクション本体は再帰的にパースされる。
       */
      {
        /** @brief 閉じタグと同一キーの開きタグの位置を直接探索 */
        auto const tag_size = 5 + close_key.size();
        auto body_start_pos = pos;

        /** @brief ネストした同一キーのセクションを考慮して深さをカウント */
        int depth2 = 1;
        std::size_t search = pos;
        std::size_t close_pos = std::string_view::npos;

        while (search < tmpl.size()) {
          auto next_open = constexpr_find_tag(tmpl, "{{#", close_key, "}}", search);
          auto next_close = constexpr_find_tag(tmpl, "{{/", close_key, "}}", search);
          if (next_close == std::string_view::npos) {
            break;
          }
          if (next_open != std::string_view::npos && next_open < next_close) {
            ++depth2;
            search = next_open + tag_size;
          } else {
            --depth2;
            if (depth2 == 0) {
              close_pos = next_close;
              break;
            }
            search = next_close + tag_size;
          }
        }

        /** @brief セクション本体の生文字列 */
        std::string_view body;
        if (close_pos != std::string_view::npos) {
          body = tmpl.substr(body_start_pos, close_pos - body_start_pos);
          if (lstrip_blocks) body = trim_tail_whitespace_for_lstrip(body);
          pos = close_pos + tag_size;
          if (trim_blocks && pos < tmpl.size() && tmpl[pos] == '\n') ++pos;
        } else {
          body = tmpl.substr(body_start_pos);
          pos = tmpl.size();
        }

        /** @brief トップレベルの {{else}} を検出して分割 */
        auto else_pos = find_toplevel_else(body);
        std::string_view main_body, else_body;
        if (else_pos != std::string_view::npos) {
          main_body = body.substr(0, else_pos);
          if (lstrip_blocks) main_body = trim_tail_whitespace_for_lstrip(main_body);
          auto else_tag_end = constexpr_find(body, "}}", else_pos + 2);
          else_body = (else_tag_end != std::string_view::npos) ? body.substr(else_tag_end + 2) : std::string_view{};
          if (lstrip_blocks) else_body = trim_tail_whitespace_for_lstrip(else_body);
        } else {
          main_body = body;
          else_body = {};
        }

        /** @brief セクションチャンクを追加し、本体を再帰パース */
        auto chunk_idx = ctx.push_section(key, 0, 0);
        auto body_start_idx = ctx.tmpl.size;
        ct_parse_into(ctx, main_body, trim_blocks, lstrip_blocks);
        auto body_end_idx = ctx.tmpl.size;

        if (else_pos != std::string_view::npos) {
          auto else_start_idx = ctx.tmpl.size;
          ct_parse_into(ctx, else_body, trim_blocks, lstrip_blocks);
          auto else_end_idx = ctx.tmpl.size;
          ctx.update_section(chunk_idx, body_start_idx, body_end_idx, else_start_idx, else_end_idx);
        } else {
          ctx.update_section(chunk_idx, body_start_idx, body_end_idx);
        }
      }
      continue;
    }

    // -- `^` で始まるタグ: 逆セクション --
    if (inner.starts_with("^")) {
      /** @brief `^` 以降のキー部分 */
      auto key = trim_sv(inner.substr(1));

      // -- {{^loop.X}} 逆セクションの処理 --
      if (auto k = parse_loop_kind(key); k) {
        at_var_kind var_kind = *k;
        auto body_start_pos = pos;
        auto close_pos = constexpr_find_tag(tmpl, "{{/", key, "}}", pos);
        if (close_pos != std::string_view::npos) {
          auto body = tmpl.substr(body_start_pos, close_pos - body_start_pos);
          if (lstrip_blocks) body = trim_tail_whitespace_for_lstrip(body);
          pos = close_pos + 5 + key.size();
          if (trim_blocks && pos < tmpl.size() && tmpl[pos] == '\n') ++pos;

          auto chunk_idx = ctx.push_at_section(var_kind, 0, 0, true);
          auto body_start_idx = ctx.tmpl.size;
          ct_parse_into(ctx, body, trim_blocks, lstrip_blocks);
          auto body_end_idx = ctx.tmpl.size;

          ctx.update_at_section(chunk_idx, body_start_idx, body_end_idx);
        }
        continue;
      }

      // -- {{^exists var}} → {{^var}} --
      auto close_key = key;
      if (key.starts_with("exists ") || key.starts_with("exists\t")) {
        if (key.size() == 7) continue;
        close_key = key;
        key = trim_sv(key.substr(7));
        if (key.empty()) continue;
      }

      // -- {{^key}} 通常の逆セクションの処理 --
      {
        auto const tag_size = 5 + close_key.size();
        auto body_start_pos = pos;

        int depth2 = 1;
        std::size_t search = pos;
        std::size_t close_pos = std::string_view::npos;

        while (search < tmpl.size()) {
          auto next_open = constexpr_find_tag(tmpl, "{{^", close_key, "}}", search);
          auto next_close = constexpr_find_tag(tmpl, "{{/", close_key, "}}", search);
          if (next_close == std::string_view::npos) {
            break;
          }
          if (next_open != std::string_view::npos && next_open < next_close) {
            ++depth2;
            search = next_open + tag_size;
          } else {
            --depth2;
            if (depth2 == 0) {
              close_pos = next_close;
              break;
            }
            search = next_close + tag_size;
          }
        }

        std::string_view body;
        if (close_pos != std::string_view::npos) {
          body = tmpl.substr(body_start_pos, close_pos - body_start_pos);
          if (lstrip_blocks) body = trim_tail_whitespace_for_lstrip(body);
          pos = close_pos + tag_size;
          if (trim_blocks && pos < tmpl.size() && tmpl[pos] == '\n') ++pos;
        } else {
          body = tmpl.substr(body_start_pos);
          pos = tmpl.size();
        }

        auto else_pos = find_toplevel_else(body);
        std::string_view main_body, else_body;
        if (else_pos != std::string_view::npos) {
          main_body = body.substr(0, else_pos);
          if (lstrip_blocks) main_body = trim_tail_whitespace_for_lstrip(main_body);
          auto else_tag_end = constexpr_find(body, "}}", else_pos + 2);
          else_body = (else_tag_end != std::string_view::npos) ? body.substr(else_tag_end + 2) : std::string_view{};
          if (lstrip_blocks) else_body = trim_tail_whitespace_for_lstrip(else_body);
        } else {
          main_body = body;
          else_body = {};
        }

        auto chunk_idx = ctx.push_inverted(key, 0, 0);
        auto body_start_idx = ctx.tmpl.size;
        ct_parse_into(ctx, main_body, trim_blocks, lstrip_blocks);
        auto body_end_idx = ctx.tmpl.size;

        if (else_pos != std::string_view::npos) {
          auto else_start_idx = ctx.tmpl.size;
          ct_parse_into(ctx, else_body, trim_blocks, lstrip_blocks);
          auto else_end_idx = ctx.tmpl.size;
          ctx.update_section(chunk_idx, body_start_idx, body_end_idx, else_start_idx, else_end_idx);
        } else {
          ctx.update_section(chunk_idx, body_start_idx, body_end_idx);
        }
      }
      continue;
    }

    // -- {{root}} — ルートコンテキスト全体 --
    if (inner == "root") {
      ctx.push_placeholder(inner, false);
      continue;
    }

    // -- root.field（パス情報を保持するためプレースホルダとして扱う） --
    if (inner.starts_with("root.")) {
      ctx.push_placeholder(inner, false);
      continue;
    }

    // -- loop.X（単体）の処理 --
    /**
     * @brief loop.X タグの解析
     * @details `{{loop.index}}` / `{{loop.is_first}}` / `{{loop.is_last}}` の形式を解析する。
     *          セクションではなく単一の値としてレンダリングされる。
     */
    if (auto k = parse_loop_kind(inner); k) {
      ctx.push_at_var(*k);
      continue;
    }

    // -- {{root}} — ルートコンテキスト全体は NTTP 経路では非対応（バイトコード VM のみ） --
    // -- {{root.X}} — ルートコンテキストフィールド参照（後段の @root. 分岐で処理） --

    // -- 通常のプレースホルダ --
    {
      auto parts = split_by_pipe(inner);
      auto key = parts[0];
      std::vector<string_filter_entry> filter_list;
      std::vector<int_filter_entry> int_filter_list;
      std::vector<float_filter_entry> float_filter_list;
      for (std::size_t fi = 1; fi < parts.size(); ++fi) {
        auto sf = parse_string_filter(parts[fi]);
        if (sf) {
          filter_list.push_back(*sf);
          continue;
        }
        auto ifl = parse_int_filter(parts[fi]);
        if (ifl) {
          int_filter_list.push_back(*ifl);
          continue;
        }
        auto ffl = parse_float_filter(parts[fi]);
        if (ffl) {
          float_filter_list.push_back(*ffl);
          continue;
        }
      }
      ctx.push_placeholder(key, false, filter_list, int_filter_list, float_filter_list);
    }
  }
}

} // namespace injamm::detail
