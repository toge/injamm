#pragma once

#include "glz_dispatch.hpp"
#include "loop_state.hpp"
#include "serialize_value.hpp"
#include <string_view>

namespace injamm::detail {

/**
 * @brief ネストパスの 1 ステップを解決する（前方宣言）
 * @tparam Buffer 出力バッファの型
 * @tparam T 値の型
 * @param out 出力バッファ
 * @param first パスの先頭キー
 * @param rest パスの残り（空文字列の場合は末端）
 * @param value 現在のコンテキスト値
 * @param loop ループ状態（なければ nullptr）
 * @return 解決成功時に true
 */
template <class Buffer, class T>
inline bool resolve_path_step(Buffer& out, std::string_view first, std::string_view rest,
                               T const& value, loop_state const* loop);

/**
 * @brief キーを解決してバッファに書き込む（トップレベルエントリ）
 * @tparam Buffer 出力バッファの型
 * @tparam T コンテキスト値の型
 * @param out 出力バッファ
 * @param key フィールド名（@prefix, ドット区切りネストパス, フラットキーのいずれか）
 * @param value コンテキスト値
 * @param loop ループコンテキスト（なければ nullptr）
 * @return キーが見つかって解決できた場合は true
 * @details キーの先頭文字で以下の 3 系統に振り分ける:
 *          - '@' 始まり → @index / @first / @last の特殊変数
 *          - ドットを含む → ネストパスとして resolve_path_step に委譲
 *          - それ以外 → write_value で平坦キーとして解決
 */
template <class Buffer, class T>
inline bool resolve_value(Buffer& out, std::string_view key, T const& value, loop_state const* loop) {
  /** @prefix — 仕様注記: chunk_at_var によりバイトコード実行では未到達 */
  if (!key.empty() && key[0] == '@') {
    if (!loop) {
      return false;
    }
    if (key == "@index") {
      serialize_value(out, loop->index);
      return true;
    }
    if (key == "@first") {
      serialize_value(out, loop->is_first());
      return true;
    }
    if (key == "@last") {
      serialize_value(out, loop->is_last());
      return true;
    }
    return false;
  }

  /** ネストパス（ドット区切り）の場合は最初のキーと残りに分割 */
  auto const dot = key.find('.');
  if (dot != std::string_view::npos) {
    auto const first = key.substr(0, dot);
    auto const rest = key.substr(dot + 1);
    return resolve_path_step(out, first, rest, value, loop);
  }

  /** フラットキー（ドットなし）: write_value で直接解決 */
  return write_value(out, key, value, loop);
}

/**
 * @brief ネストパスの 1 ステップを解決する
 * @tparam Buffer 出力バッファの型
 * @tparam T 値の型
 * @param out 出力バッファ
 * @param first 現在のステップで解決するキー
 * @param rest 残りのパス（空文字列で末端）
 * @param value 現在のコンテキスト値
 * @param loop ループ状態
 * @return 解決成功時に true
 * @details Glaze のリフレクションによりフィールド名をコンパイル時に展開し、
 *          一致するフィールドを見つけたら rest が空なら write_value に委譲、
 *          空でなければ resolve_value を再帰的に呼び出す。
 *          リフレクション不可能な型に到達した場合は false を返す。
 */
template <class Buffer, class T>
inline bool resolve_path_step(Buffer& out, std::string_view first, std::string_view rest,
                               T const& value, loop_state const* loop) {
  if constexpr (glz_reflectable<T>) {
    constexpr auto count = static_cast<std::size_t>(glz::reflect<T>::size);
    bool found = false;
    auto tied = glz::to_tie(value);
    /** fold 式で全フィールドを走査: 発見後は short-circuit で残りをスキップ */
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      (([&] {
        if (found) {
          return;
        }
        if (std::string_view{glz::reflect<T>::keys[I]} != first) {
          return;
        }
        auto const& field = glz::get<I>(tied);
        using FieldType = std::remove_cvref_t<decltype(field)>;
        if constexpr (glz_reflectable<FieldType>) {
          /** フィールドがさらにリフレクション可能なら再帰 */
          found = resolve_value(out, rest, field, loop);
        } else {
          /** 末端フィールドがリフレクション不可能な場合はパス解決不可 */
          found = false;
        }
      }()),
          ...);
    }(std::make_index_sequence<count>{});
    return found;
  } else {
    return false;
  }
}

/**
 * @brief if 式を評価する
 * @tparam T コンテキスト値の型
 * @param expr @last / @first / @index の特殊変数、またはフィールド名
 * @param value コンテキスト値
 * @param loop ループコンテキスト（なければ nullptr）
 * @return 条件が真と評価される場合は true
 * @details 評価ルール:
 *          - @prefix: ループ変数に基づくブール判定
 *          - フィールド名: resolve_value で文字列化し、空 / "false" / "0" を偽とする
 *          @note この関数は NTTP コンパイル時レンダラ用であり、
 *                バイトコード VM は別途 bc_executor 内で同等の条件評価を行う。
 */
template <class T>
inline bool evaluate_if_expr(std::string_view expr, T const& value, loop_state const* loop) {
  /** @prefix の特殊変数を判定 */
  if (!expr.empty() && expr[0] == '@') {
    if (!loop) {
      return false;
    }
    if (expr == "@last") {
      return loop->is_last();
    }
    if (expr == "@first") {
      return loop->is_first();
    }
    if (expr == "@index") {
      return loop->index > 0;
    }
    return false;
  }

  /** フィールド名として解決して truthiness 判定 */
  std::string tmp;
  if (!resolve_value(tmp, expr, value, loop)) {
    return false;
  }
  /** 非空かつ "false" / "0" でなければ truthy */
  return !tmp.empty() && tmp != "false" && tmp != "0";
}

} // namespace injamm::detail
