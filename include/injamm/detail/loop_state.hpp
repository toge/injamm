#pragma once

#include <cstdint>

namespace injamm::detail {

/** @brief ループコンテキスト
 *
 *  セクション内のループ状態を保持し、@index / @first / @last の解決に使用する。
 *  count のデフォルトを 1 とすることで、トップレベルでは @last が false になるよう設計している。
 */
struct loop_state {
  std::uint32_t index = 0;  /**< 現在のループインデックス（0始まり） */
  std::uint32_t count = 1;  /**< ループ総数。デフォルト1によりトップレベルで @last を偽にする */
  std::string_view key{};   /**< 現在の要素のキー名（@key 用、マップ反復時のみ設定） */

  /** @brief 最初の要素か判定する
   *  @return 最初の要素なら true
   */
  [[nodiscard]] constexpr bool is_first() const noexcept { return index == 0; }

  /** @brief 最後の要素か判定する
   *
   *  count > 1 ガードによりトップレベルコンテキストを "last" と判定しない。
   *
   *  @return 最後の要素なら true
   */
  [[nodiscard]] constexpr bool is_last() const noexcept {
    return count > 1 && index + 1 >= count;
  }
};

} // namespace injamm::detail
