#pragma once

#include "loop_state.hpp"
#include "serialize_value.hpp"
#include <glaze/glaze.hpp>
#include <string_view>
#include <utility>

namespace injamm::detail {

/**
 * @brief glaze リフレクション可能な型を判定するコンセプト
 * @tparam T 判定対象の型
 * @details glz::reflect<T>::size が有効な式であればリフレクション可能とみなす。
 *          glz::meta 特殊化を持つすべての型が該当する。
 */
template <class T>
concept glz_reflectable = requires {
  glz::reflect<T>::size;
};

/**
 * @brief glaze リフレクションを用いてコンテキストからフィールドを検索し出力バッファに書き込む
 * @tparam Buffer 出力バッファ型（std::string など）
 * @tparam T コンテキスト値の型
 * @param out 出力バッファ
 * @param key 検索するフィールド名
 * @param value コンテキスト値
 * @param loop ループ状態（現在未使用、将来の拡張用）
 * @return キーが見つかった場合は true、見つからない場合は false
 * @details glz_reflectable な型に対しては glz::to_tie でフィールドを展開し、
 *          線形探索でキーを照合する。見つかったフィールドは serializable_v の場合は
 *          serialize_value で出力する。T そのものが serializable_v の場合は
 *          フィールド探索を行わずに直接シリアライズする。
 */
template <class Buffer, class T>
inline bool write_value(Buffer& out, std::string_view key, T const& value, loop_state const* /*loop*/) {
  if constexpr (glz_reflectable<T>) {
    /** @brief リフレクションで取得したフィールド数 */
    constexpr auto count = static_cast<std::size_t>(glz::reflect<T>::size);
    bool found = false;
    /** @brief 全フィールドをタプルとして取得 */
    auto tied = glz::to_tie(value);
    /** @brief フィールドを線形探索し、キーが一致したらシリアライズ */
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      (([&] {
        if (found) {
          return;
        }
        if (std::string_view{glz::reflect<T>::keys[I]} != key) {
          return;
        }
        auto const& field = glz::get<I>(tied);
        using FieldType = std::remove_cvref_t<decltype(field)>;
        if constexpr (serializable_v<FieldType>) {
          serialize_value(out, field);
          found = true;
        } else if constexpr (is_std_optional_v<FieldType>) {
          if (field.has_value()) {
            serialize_value(out, *field);
          }
          found = true;
        }
      }()),
          ...);
    }(std::make_index_sequence<count>{});
    return found;
  } else if constexpr (serializable_v<T>) {
    serialize_value(out, value);
    return true;
  } else {
    return false;
  }
}

} // namespace injamm::detail
