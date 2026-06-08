#pragma once

#include "types.hpp"
#include "ct_chunk.hpp"
#include "escape.hpp"
#include "loop_state.hpp"
#include "resolve.hpp"
#include "serialize_value.hpp"
#include <cmath>
#include <expected>
#include <glaze/glaze.hpp>
#include <sstream>
#include <string>
#include <string_view>

namespace injamm::detail {

/**
 * @brief vector-like 型を判定するコンセプト
 * @details `std::string` と `std::string_view` を除外した上で、
 *          `value_type`・`size()`・`operator[]`・イテレータを持つ型を vector-like と見なす。
 *          ループセクションの繰り返し描画に使用される。
 * @tparam T 判定対象の型
 */
template <class T>
concept ct_is_vector_like =
    !std::is_arithmetic_v<T> &&
    !std::same_as<T, std::string> &&
    !std::same_as<T, std::string_view> &&
    requires(T const& v) {
      typename T::value_type;
      { v.size() } -> std::convertible_to<std::size_t>;
      { v[0] };
      { v.begin() };
      { v.end() };
    };

/**
 * @brief glz::meta によるリフレクションが可能な型を判定するコンセプト
 * @details `glz::reflect<T>::size` がコンパイル時に評価できる場合に true となる。
 *          セクションやプレースホルダのフィールド解決に使用される。
 * @tparam T 判定対象の型
 */
template <class T>
concept ct_glz_reflectable = requires {
  glz::reflect<T>::size;
};

/**
 * @brief ct_render_chunks の前方宣言
 * @details 相互再帰が必要なため、各レンダリング関数よりも先に宣言する。
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_chunks(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t start,
                                 std::size_t end, T const& value, RootT const& root_value,
                                 loop_state const* loop) -> std::expected<void, error_ctx>;

/**
 * @brief リテラルテキストをそのまま出力バッファに追加する
 * @tparam Buffer 出力バッファの型（std::string など）
 * @tparam N      チャンク配列のサイズ
 * @param[out] out    出力バッファ
 * @param[in]  chunks チャンク配列
 * @param[in]  i      処理対象チャンクのインデックス
 */
template <class Buffer, std::size_t N>
constexpr void ct_render_literal(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i) {
  out.append(chunks.texts[i]);
}

/**
 * @brief 文字列フィルタを適用する
 * @param str 対象の文字列
 * @param filter 適用するフィルタの種別
 */
constexpr void apply_string_filter(std::string& str, string_filter_entry entry) {
  switch (entry.filter) {
    case string_filter::upper:
      for (auto& c : str) {
        if (c >= 'a' && c <= 'z') c -= 32;
      }
      break;
    case string_filter::lower:
      for (auto& c : str) {
        if (c >= 'A' && c <= 'Z') c += 32;
      }
      break;
    case string_filter::capitalize:
      if (!str.empty() && str[0] >= 'a' && str[0] <= 'z') {
        str[0] -= 32;
      }
      break;
    case string_filter::title: {
      bool new_word = true;
      for (auto& c : str) {
        if (c == ' ' || c == '\t') {
          new_word = true;
        } else if (new_word && c >= 'a' && c <= 'z') {
          c -= 32;
          new_word = false;
        } else {
          new_word = false;
        }
      }
      break;
    }
    case string_filter::trim: {
      auto start = str.find_first_not_of(" \t");
      if (start == std::string::npos) {
        str.clear();
      } else {
        auto end = str.find_last_not_of(" \t");
        str = str.substr(start, end - start + 1);
      }
      break;
    }
    case string_filter::ltrim: {
      auto start = str.find_first_not_of(" \t");
      if (start == std::string::npos) {
        str.clear();
      } else {
        str = str.substr(start);
      }
      break;
    }
    case string_filter::rtrim: {
      auto end = str.find_last_not_of(" \t");
      if (end == std::string::npos) {
        str.clear();
      } else {
        str = str.substr(0, end + 1);
      }
      break;
    }
    case string_filter::left: {
      auto width = entry.arg1;
      if (str.size() < static_cast<std::size_t>(width)) {
        auto pad = width - str.size();
        str = std::string(pad, ' ') + str;
      }
      break;
    }
    case string_filter::right: {
      auto width = entry.arg1;
      if (str.size() < static_cast<std::size_t>(width)) {
        auto pad = width - str.size();
        str = str + std::string(pad, ' ');
      }
      break;
    }
    case string_filter::center: {
      auto width = entry.arg1;
      if (str.size() < static_cast<std::size_t>(width)) {
        auto pad = width - str.size();
        auto left_pad = pad / 2;
        auto right_pad = pad - left_pad;
        str = std::string(left_pad, ' ') + str + std::string(right_pad, ' ');
      }
      break;
    }
    case string_filter::truncate: {
      auto max_len = entry.arg1;
      if (str.size() > static_cast<std::size_t>(max_len) && max_len >= 3) {
        str = str.substr(0, max_len - 3) + "...";
      } else if (str.size() > static_cast<std::size_t>(max_len)) {
        str = str.substr(0, max_len);
      }
      break;
    }
    case string_filter::substr: {
      auto start = entry.arg1;
      auto length = entry.arg2;
      if (start >= 0 && static_cast<std::size_t>(start) < str.size()) {
        if (length > 0) {
          str = str.substr(start, length);
        } else {
          str = str.substr(start);
        }
      } else {
        str.clear();
      }
      break;
    }
  }
}

/**
 * @brief 整数フィルタを適用する
 * @param str 対象の文字列
 * @param entry 適用するフィルタの種別と引数
 */
constexpr void apply_int_filter(std::string& str, int_filter_entry entry) {
  switch (entry.filter) {
    case int_filter::abs: {
      try {
        // 小数点を含む場合は実数として扱う
        if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
          double val = std::stod(str);
          str = std::to_string(std::abs(val));
        } else {
          long long val = std::stoll(str);
          str = std::to_string(std::abs(val));
        }
      } catch (...) {
        // 変換失敗: そのまま
      }
      break;
    }
    case int_filter::hex: {
      try {
        long long val = std::stoll(str);
        std::ostringstream oss;
        oss << std::hex << val;
        str = oss.str();
      } catch (...) {
        // 変換失敗: そのまま
      }
      break;
    }
    case int_filter::oct: {
      try {
        long long val = std::stoll(str);
        std::ostringstream oss;
        oss << std::oct << val;
        str = oss.str();
      } catch (...) {
        // 変換失敗: そのまま
      }
      break;
    }
    case int_filter::bin: {
      try {
        long long val = std::stoll(str);
        str = "";
        if (val == 0) {
          str = "0";
        } else {
          while (val > 0) {
            str = (val % 2 == 0 ? "0" : "1") + str;
            val /= 2;
          }
        }
      } catch (...) {
        // 変換失敗: そのまま
      }
      break;
    }
    case int_filter::neg: {
      try {
        if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
          double val = std::stod(str);
          str = std::to_string(-val);
        } else {
          long long val = std::stoll(str);
          str = std::to_string(-val);
        }
      } catch (...) {
        // 変換失敗: そのまま
      }
      break;
    }
    case int_filter::mod: {
      try {
        long long val = std::stoll(str);
        auto divisor = entry.arg;
        if (divisor != 0) {
          str = std::to_string(val % divisor);
        }
      } catch (...) {
        // 変換失敗: そのまま
      }
      break;
    }
    case int_filter::numify: {
      try {
        // 小数点を含む場合は実数としてカンマ区切りを適用
        if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
          double val = std::stod(str);
          bool negative = val < 0;
          if (negative) val = -val;
          auto int_part = static_cast<long long>(val);
          std::string num = std::to_string(int_part);
          std::string result;
          int count = 0;
          for (int i = num.size() - 1; i >= 0; --i) {
            result = num[i] + result;
            count++;
            if (count % 3 == 0 && i > 0) {
              result = ',' + result;
            }
          }
          // 小数部分を処理
          auto frac = val - static_cast<double>(int_part);
          if (frac != 0.0) {
            auto dot_pos = str.find('.');
            std::size_t prec = 6;
            if (dot_pos != std::string::npos) {
              prec = str.size() - dot_pos - 1;
              if (prec > 6) prec = 6;
              if (prec == 0) prec = 1;
            }
            std::ostringstream oss;
            oss << std::fixed;
            oss.precision(prec);
            oss << frac;
            auto frac_str = oss.str();
            if (frac_str.size() > 2) {
              frac_str = frac_str.substr(1);
            }
            result += frac_str;
          }
          str = negative ? "-" + result : result;
        } else {
          long long val = std::stoll(str);
          bool negative = val < 0;
          if (negative) val = -val;
          std::string num = std::to_string(val);
          std::string result;
          int count = 0;
          for (int i = num.size() - 1; i >= 0; --i) {
            result = num[i] + result;
            count++;
            if (count % 3 == 0 && i > 0) {
              result = ',' + result;
            }
          }
          str = negative ? "-" + result : result;
        }
      } catch (...) {
        // 変換失敗: そのまま
      }
      break;
    }
  }
}

/**
 * @brief 実数フィルタを適用する
 * @param str 対象の文字列
 * @param entry 適用するフィルタの種別と引数
 */
constexpr void apply_float_filter(std::string& str, float_filter_entry entry) {
  switch (entry.filter) {
    case float_filter::precision: {
      try {
        double val = std::stod(str);
        std::ostringstream oss;
        oss << std::fixed;
        oss.precision(entry.arg);
        oss << val;
        str = oss.str();
      } catch (...) {
        // 変換失敗: そのまま
      }
      break;
    }
  }
}

/**
 * @brief プレースホルダ（{{var}} / {{{var}}}）をレンダリングする
 * @details キーの種類に応じて以下の分岐を行う:
 *          - `@root.field`: ルートオブジェクトのフィールドを解決
 *          - `@root`: ルートオブジェクト全体をシリアライズ
 *          - `@index` / `@first` / `@last`: ループ状態の値を出力
 *          - 通常キー: 現在のコンテキストからフィールドを解決
 *          mustache モード時は HTML エスケープを適用し、raw モード（{{{}}}）ではエスケープなし。
 * @tparam Mode   レンダリングモード（mustache_tag または stencil_tag）
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out        出力バッファ
 * @param[in]  chunks     チャンク配列
 * @param[in]  i          処理対象チャンクのインデックス
 * @param[in]  value      現在のコンテキスト値
 * @param[in]  root_value ルートコンテキスト値（@root 参照用）
 * @param[in]  loop       現在のループ状態（@index/@first/@last 参照用、nullptr 可）
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_placeholder(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                      T const& value, RootT const& root_value, loop_state const* loop)
    -> std::expected<void, error_ctx> {
  auto const key = chunks.texts[i];
  bool raw = chunks.flags[i] != 0;

  auto const& filters = chunks.filters[i];
  auto const& int_filters = chunks.int_filters[i];
  auto const& float_filters = chunks.float_filters[i];

  // フィルタが存在する場合
  if (!filters.empty() || !int_filters.empty() || !float_filters.empty()) {
    std::string tmp;
    if (!resolve_value(tmp, key, value, loop)) {
      return std::unexpected(error_ctx{.ec = error_code::unknown_key});
    }
    // 文字列フィルタ適用
    for (auto f : filters) {
      apply_string_filter(tmp, f);
    }
    // 整数フィルタ適用
    for (auto f : int_filters) {
      apply_int_filter(tmp, f);
    }
    // 実数フィルタ適用
    for (auto f : float_filters) {
      apply_float_filter(tmp, f);
    }
    // 出力
    if constexpr (std::is_same_v<Mode, mustache_tag>) {
      if (!raw) {
        html_escape_into(out, std::string_view{tmp});
      } else {
        out.append(tmp);
      }
    } else {
      out.append(tmp);
    }
    return {};
  }

  /**
   * @brief `@root.field` 形式の処理
   * @details `@root.` プレフィックスでルートオブジェクトのフィールドを直接参照する。
   *          ルートコンテキストが現在のコンテキストと異なる場合に有用。
   */
  if (key.starts_with("@root.")) {
    auto const rest = key.substr(6);
    if constexpr (std::is_same_v<Mode, mustache_tag>) {
      if (!raw) {
        std::string tmp;
        if (!resolve_value(tmp, rest, root_value, nullptr)) {
          return std::unexpected(error_ctx{.ec = error_code::unknown_key});
        }
        html_escape_into(out, std::string_view{tmp});
        return {};
      }
    }
    if (!resolve_value(out, rest, root_value, nullptr)) {
      return std::unexpected(error_ctx{.ec = error_code::unknown_key});
    }
    return {};
  }

  /**
   * @brief `@root` 単体の処理
   * @details ルートオブジェクト全体を JSON 形式などでシリアライズして出力する。
   *          ルート型がシリアライズ可能でない場合はエラーを返す。
   */
  if (key == "@root") {
    if constexpr (serializable_v<RootT>) {
      serialize_value(out, root_value);
    } else {
      return std::unexpected(error_ctx{.ec = error_code::type_mismatch});
    }
    return {};
  }

  /**
   * @brief @index / @first / @last の処理
   * @details ループ状態から対応する値を出力する。
   *          ループ外で参照された場合は何も出力しない（空文字）。
   */
  if (key.starts_with("@")) {
    if (!loop) {
      return {};
    }
    if (key == "@index") {
      serialize_value(out, loop->index);
      return {};
    }
    if (key == "@first") {
      serialize_value(out, loop->is_first());
      return {};
    }
    if (key == "@last") {
      serialize_value(out, loop->is_last());
      return {};
    }
    return {};
  }

  /**
   * @brief 通常のフィールド解決
   * @details mustache モードでは HTML エスケープを適用して出力する。
   *          stencil モード（raw）ではそのまま出力する。
   *          キーが未知の場合は unknown_key エラーを返す。
   */
  if constexpr (std::is_same_v<Mode, mustache_tag>) {
    if (!raw) {
      std::string tmp;
      if (!resolve_value(tmp, key, value, loop)) {
        return std::unexpected(error_ctx{.ec = error_code::unknown_key});
      }
      html_escape_into(out, std::string_view{tmp});
      return {};
    }
  }
  if (!resolve_value(out, key, value, loop)) {
    return std::unexpected(error_ctx{.ec = error_code::unknown_key});
  }
  return {};
}

/**
 * @brief セクション（{{#key}}...{{/key}}）をレンダリングする
 * @details キーに対応するフィールドの型に応じて動作が分岐する:
 *          - vector-like 型: 各要素に対して本体を繰り返し描画（ループ）
 *          - bool 型: true の場合に本体を 1 回描画
 *          - その他の型: type_mismatch エラー
 * @tparam Mode   レンダリングモード
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out          出力バッファ
 * @param[in]  chunks       チャンク配列
 * @param[in]  i            処理対象チャンクのインデックス
 * @param[in]  value        現在のコンテキスト値
 * @param[in]  root_value   ルートコンテキスト値
 * @param[in]  parent_loop  親ループのループ状態（ネスト時継承用）
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_section(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                  T const& value, RootT const& root_value, loop_state const* parent_loop)
    -> std::expected<void, error_ctx> {
  auto const key = chunks.texts[i];
  auto body_start = chunks.body_starts[i];
  auto body_end = chunks.body_ends[i];

  if constexpr (ct_glz_reflectable<T>) {
    constexpr auto sz = static_cast<std::size_t>(glz::reflect<T>::size);
    bool found = false;
    std::expected<void, error_ctx> res{};
    auto tied = glz::to_tie(value);
    /**
     * @brief リフレクションで全フィールドを走査し、キーに一致するフィールドを処理する
     * @details 展開されたインデックスシーケンスで各フィールドを順次比較。
     *          found フラグで最初に一致したフィールドのみを処理する（ショートサーキット）。
     */
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      (([&] {
        if (found) return;
        if (std::string_view{glz::reflect<T>::keys[I]} != key) return;
        found = true;
        auto const& field = glz::get<I>(tied);
        using FT = std::remove_cvref_t<decltype(field)>;
        /**
         * @brief vector-like 型の場合: ループ
         * @details 各要素に対して本体チャンクを再帰的にレンダリングする。
         *          loop_state に件数と現在のインデックスを設定して渡す。
         */
        if constexpr (ct_is_vector_like<FT>) {
          loop_state ls;
          ls.count = static_cast<std::uint32_t>(field.size());
          ls.in_loop = true;
          for (ls.index = 0; ls.index < static_cast<std::uint32_t>(field.size()); ++ls.index) {
            res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, field[ls.index], root_value, &ls);
            if (!res) return;
          }
        /**
         * @brief bool 型の場合: 条件分岐
         * @details true の場合のみ本体を 1 回描画する。
         *          現在のコンテキスト value をそのまま渡す（ループは発生しない）。
         */
        } else if constexpr (std::same_as<FT, bool>) {
          if (field) {
            res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, parent_loop);
          }
        } else {
          res = std::unexpected(error_ctx{.ec = error_code::type_mismatch});
        }
      }()), ...);
    }(std::make_index_sequence<sz>{});

    if (!found) {
      return std::unexpected(error_ctx{.ec = error_code::unknown_key});
    }
    return res;
  } else {
    return std::unexpected(error_ctx{.ec = error_code::type_mismatch});
  }
}

/**
 * @brief 逆セクション（{{^key}}...{{/key}}）をレンダリングする
 * @details キーに対応するフィールドの型に応じて動作が分岐する:
 *          - vector-like 型: 空の場合に本体を描画
 *          - bool 型: false の場合に本体を描画
 *          - その他の型: type_mismatch エラー
 *          セクション（{{#key}}）の真偽が逆転した動作。
 * @tparam Mode   レンダリングモード
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out          出力バッファ
 * @param[in]  chunks       チャンク配列
 * @param[in]  i            処理対象チャンクのインデックス
 * @param[in]  value        現在のコンテキスト値
 * @param[in]  root_value   ルートコンテキスト値
 * @param[in]  parent_loop  親ループのループ状態
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_inverted(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                   T const& value, RootT const& root_value, loop_state const* parent_loop)
    -> std::expected<void, error_ctx> {
  auto const key = chunks.texts[i];
  auto body_start = chunks.body_starts[i];
  auto body_end = chunks.body_ends[i];

  if constexpr (ct_glz_reflectable<T>) {
    constexpr auto sz = static_cast<std::size_t>(glz::reflect<T>::size);
    bool found = false;
    std::expected<void, error_ctx> res{};
    auto tied = glz::to_tie(value);
    /**
     * @brief リフレクションで全フィールドを走査し、キーに一致するフィールドを処理する
     * @details ct_render_section と同様のパターンだが、条件が反転している:
     *          - vector-like が空 → 本体描画
     *          - bool が false → 本体描画
     */
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      (([&] {
        if (found) return;
        if (std::string_view{glz::reflect<T>::keys[I]} != key) return;
        found = true;
        auto const& field = glz::get<I>(tied);
        using FT = std::remove_cvref_t<decltype(field)>;
        if constexpr (ct_is_vector_like<FT>) {
          /** @brief 空配列の場合のみ本体を描画 */
          if (field.empty()) {
            res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, parent_loop);
          }
         } else if constexpr (std::same_as<FT, bool>) {
           /** bool 型の場合: 条件分岐 */
           if (field) {
             res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, parent_loop);
           }
         } else if constexpr (ct_glz_reflectable<FT>) {
           /** 構造体の場合: 全フィールドを反復 */
           constexpr auto sz = glz::reflect<FT>::size;
           auto tied = glz::to_tie(field);
           [&]<std::size_t... J>(std::index_sequence<J...>) {
             (([&] {
               if (!res) return;
               loop_state ls;
               ls.count = sz;
               ls.index = J;
               ls.key = glz::reflect<FT>::keys[J];
               ls.in_loop = true;
               res = ct_render_chunks<Mode>(out, chunks, body_start, body_end, glz::get<J>(tied), root_value, &ls);
             }()), ...);
           }(std::make_index_sequence<sz>{});
         } else {
           res = std::unexpected(error_ctx{.ec = error_code::type_mismatch});
         }
       }()), ...);

    }(std::make_index_sequence<sz>{});

    if (!found) {
      return std::unexpected(error_ctx{.ec = error_code::unknown_key});
    }
    return res;
  } else {
    return std::unexpected(error_ctx{.ec = error_code::type_mismatch});
  }
}

/**
 * @brief @var タグ（{{@index}} / {{@first}} / {{@last}} / {{@root}}）をレンダリングする
 * @details 単体の @var チャンクを処理し、対応する値を出力する。
 *          @index / @first / @last はループ状態が nullptr の場合は何も出力しない。
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out        出力バッファ
 * @param[in]  chunks     チャンク配列
 * @param[in]  i          処理対象チャンクのインデックス
 * @param[in]  value      現在のコンテキスト値（@var では未使用）
 * @param[in]  root_value ルートコンテキスト値（@root 用）
 * @param[in]  loop       現在のループ状態（nullptr 可）
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_at_var(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                 T const& /*value*/, RootT const& root_value, loop_state const* loop)
    -> std::expected<void, error_ctx> {
  auto var = static_cast<ct_at_var_kind>(chunks.flags[i]);
  switch (var) {
    case ct_at_var_kind::index:
      if (!loop) return {};
      serialize_value(out, loop->index);
      break;
    case ct_at_var_kind::first:
      if (!loop) return {};
      serialize_value(out, loop->is_first());
      break;
    case ct_at_var_kind::last:
      if (!loop) return {};
      serialize_value(out, loop->is_last());
      break;
    case ct_at_var_kind::root:
      if constexpr (serializable_v<RootT>) {
        serialize_value(out, root_value);
      } else {
        return std::unexpected(error_ctx{.ec = error_code::type_mismatch});
      }
      break;
    case ct_at_var_kind::key:
      if (!loop) return {};
      if (!loop->key.empty()) {
        serialize_value(out, loop->key);
      }
      break;
  }
  return {};
}

/**
 * @brief @var セクション（{{#@first}}...{{/@first}} / {{^@last}}...{{/@last}}）をレンダリングする
 * @details ループ内の位置に応じて条件判定を行い、本体を描画する。
 *          inverted フラグが true の場合は条件が反転する。
 *          各 @var 種別の条件:
 *          - index: loop->index > 0（先頭要素以外）
 *          - first: loop->is_first()（先頭要素）
 *          - last:  loop->is_last()（末尾要素）
 *          - root:  常に false（実質的には使われない）
 * @tparam Mode   レンダリングモード
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out        出力バッファ
 * @param[in]  chunks     チャンク配列
 * @param[in]  i          処理対象チャンクのインデックス
 * @param[in]  value      現在のコンテキスト値
 * @param[in]  root_value ルートコンテキスト値
 * @param[in]  loop       現在のループ状態（nullptr 可）
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_at_section(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                                     T const& value, RootT const& root_value, loop_state const* loop)
    -> std::expected<void, error_ctx> {
  auto var = static_cast<ct_at_var_kind>(chunks.flags[i]);
  auto body_start = chunks.body_starts[i];
  auto body_end = chunks.body_ends[i];
  bool inverted = chunks.else_starts[i] != 0;

  bool cond = false;
  if (loop) {
    switch (var) {
      case ct_at_var_kind::last:
        cond = loop->is_last();
        break;
      case ct_at_var_kind::first:
        cond = loop->is_first();
        break;
      case ct_at_var_kind::index:
        cond = loop->index > 0;
        break;
      case ct_at_var_kind::root:
        cond = false;
        break;
    }
  }
  /** @brief 逆セクション（{{^@var}}）の場合は条件を反転 */
  if (inverted) {
    cond = !cond;
  }
  if (cond) {
    auto r = ct_render_chunks<Mode>(out, chunks, body_start, body_end, value, root_value, loop);
    if (!r) return r;
  }
  return {};
}

/**
 * @brief if/else 条件分岐（{{#if X}}...{{else}}...{{/if}}）をレンダリングする
 * @details 条件式 X が真の場合に then 節、偽の場合に else 節を描画する。
 *          else 節が存在しない場合は本体のみとなる。
 *          条件式が `@` で始まる場合はループ状態（@last / @first / @index）を参照。
 *          通常の文字列条件は resolve_value で解決し、空文字・"false"・"0" 以外を真とする。
 * @tparam Mode   レンダリングモード
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out        出力バッファ
 * @param[in]  chunks     チャンク配列
 * @param[in]  i          処理対象チャンクのインデックス
 * @param[in]  value      現在のコンテキスト値
 * @param[in]  root_value ルートコンテキスト値
 * @param[in]  loop       現在のループ状態
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_if(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t i,
                             T const& value, RootT const& root_value, loop_state const* loop)
    -> std::expected<void, error_ctx> {
  bool cond = false;
  auto const expr = chunks.texts[i];

  /**
   * @brief `@` で始まる条件式の処理
   * @details @last / @first / @index はループ状態から直接判定する。
   *          ループ外ではすべて偽となる。
   */
  if (!expr.empty() && expr[0] == '@') {
    if (loop) {
      if (expr == "@last") cond = loop->is_last();
      else if (expr == "@first") cond = loop->is_first();
      else if (expr == "@index") cond = loop->index > 0;
    }
  } else {
    /**
     * @brief 通常の文字列条件式の解決
     * @details resolve_value で値を文字列として取得し、
     *          空文字・"false"・"0" 以外を真と判断する。
     */
    std::string tmp;
    if (resolve_value(tmp, expr, value, loop)) {
      cond = !tmp.empty() && tmp != "false" && tmp != "0";
    }
  }

  auto then_start = chunks.body_starts[i];
  auto then_end = chunks.body_ends[i];
  auto else_start = chunks.else_starts[i];
  auto else_end = chunks.else_ends[i];

  /** @brief 条件に応じて then 節または else 節の範囲を選択 */
  auto [start, end] = cond ? std::pair{then_start, then_end} : std::pair{else_start, else_end};
  auto r = ct_render_chunks<Mode>(out, chunks, start, end, value, root_value, loop);
  if (!r) return r;
  return {};
}

/**
 * @brief フラットなチャンク列を指定範囲に従って順次レンダリングする（SoA 版ディスパッチャ）
 * @details start から end までの各チャンクを種類に応じて適切なレンダリング関数に dispatch する。
 *          チャンクの種類:
 *          - literal:    そのまま出力
 *          - placeholder: 変数解決とエスケープ
 *          - section:     ループまたは条件付き描画
 *          - inverted:    逆条件描画
 *          - at_var:      @index/@first/@last/@root の値出力
 *          - at_section:   @var 条件セクション
 *          - if_else:     if/else 条件分岐
 * @tparam Mode   レンダリングモード
 * @tparam Buffer 出力バッファの型
 * @tparam T      現在のコンテキストの型
 * @tparam RootT  ルートコンテキストの型
 * @tparam N      チャンク配列のサイズ
 * @param[out] out        出力バッファ
 * @param[in]  chunks     チャンク配列
 * @param[in]  start      レンダリング開始インデックス
 * @param[in]  end        レンダリング終了インデックス（この手前まで）
 * @param[in]  value      現在のコンテキスト値
 * @param[in]  root_value ルートコンテキスト値
 * @param[in]  loop       現在のループ状態（nullptr 可）
 * @return 成功時は void、失敗時は error_ctx
 */
template <class Mode, class Buffer, class T, class RootT, std::size_t N>
constexpr auto ct_render_chunks(Buffer& out, ct_parsed_template<N> const& chunks, std::size_t start,
                                 std::size_t end, T const& value, RootT const& root_value,
                                 loop_state const* loop) -> std::expected<void, error_ctx> {
  for (std::size_t i = start; i < end; ++i) {
    switch (chunks.kinds[i]) {
      case ct_chunk_kind::literal:
        ct_render_literal(out, chunks, i);
        break;
      case ct_chunk_kind::placeholder: {
        auto r = ct_render_placeholder<Mode>(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        break;
      }
      case ct_chunk_kind::section: {
        auto r = ct_render_section<Mode>(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        break;
      }
      case ct_chunk_kind::inverted: {
        auto r = ct_render_inverted<Mode>(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        break;
      }
      case ct_chunk_kind::at_var: {
        auto r = ct_render_at_var(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        break;
      }
      case ct_chunk_kind::at_section: {
        auto r = ct_render_at_section<Mode>(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        break;
      }
      case ct_chunk_kind::if_else: {
        auto r = ct_render_if<Mode>(out, chunks, i, value, root_value, loop);
        if (!r) return r;
        break;
      }
    }
  }
  return {};
}

} // namespace injamm::detail
