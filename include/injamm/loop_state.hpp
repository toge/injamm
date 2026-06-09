#pragma once

#include <cstdint>

namespace injamm::detail {

/** @brief ループコンテキスト
 *
 *  セクション内のループ状態を保持し、@index / @first / @last の解決に使用する。
 *  in_loop フラグでループ内かどうかを区別し、トップレベルで @last が true になる
 *  のを防ぐ。
 */
struct loop_state {
  std::uint32_t index = 0;  /**< 現在のループインデックス（0始まり） */
  std::uint32_t count = 0;  /**< ループ総数 */
  bool in_loop = false;     /**< ループ内かどうか */
  std::string_view key{};   /**< 現在の要素のキー名（@key 用、マップ反復時のみ設定） */
  mutable bool break_flag = false;    /**< break 要求フラグ（子 executor からセット） */
  mutable bool continue_flag = false; /**< continue 要求フラグ（子 executor からセット） */

  /** @brief 最初の要素か判定する
   *  @return 最初の要素なら true
   */
  [[nodiscard]] constexpr bool is_first() const noexcept { return index == 0; }

  /** @brief 最後の要素か判定する
   *
   *  in_loop フラグによりトップレベルコンテキストを "last" と判定しない。
   *
   *  @return 最後の要素なら true
   */
  [[nodiscard]] constexpr bool is_last() const noexcept {
    return in_loop && count > 0 && index + 1 >= count;
  }
};

} // namespace injamm::detail
