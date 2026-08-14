#pragma once
#ifndef RENDER_RENDER19_HPP
#define RENDER_RENDER19_HPP
/**
 * @file render.hpp
 * @brief injamm_codegen によって自動生成されたレンダリング関数
 */

#include <expected>
#include <string>

#include <injamm/types.hpp>
#include <glaze/glaze.hpp>
#define INJAMM_CODEGEN_DISABLE_SIMD 1

namespace generated {
#include "codegen_helpers.hpp"


/**
 * @brief テンプレート文字列から生成されたレンダリング関数（バッファ再利用版）
 *
 * @details injamm_codegen によって自動生成された関数。
 *          出力先を引数で受け取る。std::string ならバッファを再利用し、
 *          injamm::detail::output_sink を満たす型（callback_sink 等）なら
 *          ストリーミング出力する。
 *
 * @tparam T    データ型（フィールドへのアクセスが必要）
 * @tparam Sink 出力先型（std::string または output_sink を満たす型）
 * @param data レンダリング対象のデータ
 * @param out  出力先（std::string なら内容はクリアされる）
 * @return 正常時: void。エラー時: error_ctx
 */
template <typename T, typename Sink = std::string>
  requires injamm::detail::output_sink<Sink>
[[nodiscard]] std::expected<void, injamm::error_ctx>
render19(const T& data, Sink& out) {
  if constexpr (std::is_same_v<Sink, std::string>) {
    out.clear();
    out.reserve(1);
  }
  
  auto _size1 = data.items.size();
  std::size_t _lo1 = 0, _hi1 = _size1;
  bool _bwd1 = false;
  std::size_t _st_take1 = 3u;
  std::size_t _st_skip1 = 1u;
  auto _block1 = _st_take1 + _st_skip1;
  auto _count1 = _st_take1 == 0 ? std::size_t(0) : ((_hi1 - _lo1) / _block1 * _st_take1 + std::min(_st_take1, (_hi1 - _lo1) % _block1));
  std::size_t _src1 = _bwd1 ? (_hi1 - 1) : _lo1;
  for (std::size_t _i1 = 0; _i1 < _count1; ++_i1) {
    while (!(_src1 >= _lo1 && _src1 < _hi1 && ((_bwd1 ? (_hi1 - 1 - _src1) : (_src1 - _lo1)) % _block1) < _st_take1)) { if (_bwd1) --_src1; else ++_src1; }
    const auto& _item1 = data.items[_src1];
    html_escape_append_value(out, _item1.name);
    out.append(";");
    if (_bwd1) --_src1; else ++_src1;
  }
  
  return {};
}

/**
 * @brief テンプレート文字列から生成されたレンダリング関数
 *
 * @details injamm_codegen によって自動生成された関数。
 *          バッファ再利用版 (render(data, out)) のラッパー。
 *
 * @tparam T データ型（フィールドへのアクセスが必要）
 * @param data レンダリング対象のデータ
 * @return 正常時: レンダリング結果文字列。エラー時: error_ctx
 *
 * @code
 *   // 使い方例:
 *   #include "render.hpp"
 *
 *   struct UserData { std::string name; int age; };
 *   UserData user{"Alice", 30};
 *   auto result = generated::render(user);
 *   if (result) std::cout << *result << std::endl;
 * @endcode
 */
template <typename T>
[[nodiscard]] std::expected<std::string, injamm::error_ctx>
render19(const T& data) {
  std::string out;
  auto result = render19(data, out);
  if (!result) return std::unexpected(result.error());
  return out;
}

} // namespace generated

#endif // RENDER_RENDER19_HPP
