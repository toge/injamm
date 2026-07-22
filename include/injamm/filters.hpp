#pragma once

#include "bytecode.hpp"
#include "types.hpp"
#include <array>
#include <charconv>
#include <fast_float/fast_float.h>
#include <glaze/util/zmij.hpp>
#include <cmath>
#include <expected>
#include <string>

namespace injamm::detail {

/**
 * @brief 文字列フィルタを適用する
 * @param str 対象の文字列
 * @param entry 適用するフィルタの種別と引数
 */
constexpr void apply_string_filter(std::string& str, string_filter_entry entry) {
  switch (entry.filter) {
  case string_filter::upper:
    for (auto& c : str) {
      if (c >= 'a' && c <= 'z')
        c -= 32;
    }
    break;
  case string_filter::lower:
    for (auto& c : str) {
      if (c >= 'A' && c <= 'Z')
        c += 32;
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
      str.erase(end + 1);
      str.erase(0, start);
    }
    break;
  }
  case string_filter::ltrim: {
    auto start = str.find_first_not_of(" \t");
    if (start == std::string::npos) {
      str.clear();
    } else {
      str.erase(0, start);
    }
    break;
  }
  case string_filter::rtrim: {
    auto end = str.find_last_not_of(" \t");
    if (end == std::string::npos) {
      str.clear();
    } else {
      str.erase(end + 1);
    }
    break;
  }
  case string_filter::left: {
    auto width = entry.arg1;
    if (str.size() < static_cast<std::size_t>(width)) {
      auto pad = width - str.size();
      str      = std::string(pad, ' ') + str;
    }
    break;
  }
  case string_filter::right: {
    auto width = entry.arg1;
    if (str.size() < static_cast<std::size_t>(width)) {
      auto pad = width - str.size();
      str      = str + std::string(pad, ' ');
    }
    break;
  }
  case string_filter::center: {
    auto width = entry.arg1;
    if (str.size() < static_cast<std::size_t>(width)) {
      auto pad       = width - str.size();
      auto left_pad  = pad / 2;
      auto right_pad = pad - left_pad;
      str            = std::string(left_pad, ' ') + str + std::string(right_pad, ' ');
    }
    break;
  }
  case string_filter::truncate: {
    auto max_len = entry.arg1;
    if (str.size() > static_cast<std::size_t>(max_len) && max_len >= 3) {
      str.erase(max_len - 3);
      str.append("...");
    } else if (str.size() > static_cast<std::size_t>(max_len)) {
      str.erase(max_len);
    }
    break;
  }
  case string_filter::substr: {
    auto start  = entry.arg1;
    auto length = entry.arg2;
    if (start >= 0 && static_cast<std::size_t>(start) < str.size()) {
      if (length > 0 && static_cast<std::size_t>(start + length) < str.size()) {
        str.erase(start + length);
      }
      if (start > 0) {
        str.erase(0, start);
      }
    } else {
      str.clear();
    }
    break;
  }
  case string_filter::replace: {
    if (!entry.str_arg1.empty()) {
      /** replace(old,new): str 中の str_arg1 を str_arg2 に置換 */
      std::string result;
      std::size_t pos = 0;
      while (pos < str.size()) {
        auto found = str.find(entry.str_arg1, pos);
        if (found == std::string::npos) {
          result.append(str, pos, std::string::npos);
          break;
        }
        result.append(str, pos, found - pos);
        result.append(entry.str_arg2);
        pos = found + entry.str_arg1.size();
      }
      str = std::move(result);
    } else {
      /** 引数なし replace: str 中の全 '\n' を ' ' に置換（後方互換） */
      for (auto& c : str) {
        if (c == '\n') c = ' ';
      }
    }
    break;
  }
  case string_filter::default_value: {
    if (str.empty() && !entry.str_arg1.empty()) {
      str = entry.str_arg1;
    }
    break;
  }
  case string_filter::to_json:
  case string_filter::safe:
  case string_filter::format:
    // resolve_filtered で特殊処理済み。no-op。
    break;
  case string_filter::repeat: {
    auto n = entry.arg1;
    if (n < 1) {
      str.clear();
    } else if (n > 1 && !str.empty()) {
      auto saved = str;
      str.reserve(saved.size() * static_cast<std::size_t>(n));
      for (int i = 1; i < n; ++i)
        str += saved;
    }
    break;
  }
  case string_filter::indent: {
    if (entry.arg1 > 0 && !str.empty()) {
      auto pad = std::string(static_cast<std::size_t>(entry.arg1), ' ');
      std::string result;
      std::size_t pos = 0;
      while (pos < str.size()) {
        auto nl = str.find('\n', pos);
        if (nl == std::string::npos) {
          result.append(str, pos, str.size() - pos);
          break;
        }
        result.append(str, pos, nl - pos);
        result.push_back('\n');
        result.append(pad);
        pos = nl + 1;
      }
      str = std::move(result);
    }
    break;
  }
  case string_filter::pad: {
    auto width = entry.arg1;
    if (static_cast<int>(str.size()) < width && width > 0) {
      auto total_pad = static_cast<std::size_t>(width) - str.size();
      std::string pad_str = entry.str_arg1.empty() ? " " : std::string{entry.str_arg1};
      /** 複数文字パディングの繰り返し */
      std::string padding;
      padding.reserve(total_pad);
      while (padding.size() < total_pad) {
        padding += pad_str;
      }
      if (padding.size() > total_pad) {
        padding.resize(total_pad);
      }
      str = str + padding;
    }
    break;
  }
  case string_filter::pluralize: {
    long long val{};
    auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), val);
    if (ec == std::errc()) {
      str = (val == 1) ? std::string{entry.str_arg1} : std::string{entry.str_arg2};
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
[[nodiscard]] constexpr std::expected<void, error_ctx> apply_int_filter(std::string& str, int_filter_entry entry) {
  switch (entry.filter) {
  case int_filter::abs: {
    auto data = str.data();
    auto size = str.size();
    if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
      double val{};
      if (auto [p, ec] = fast_float::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, glz::zmij::double_buffer_size> buf;
        auto end = glz::to_chars(buf.data(), std::abs(val));
        str.assign(buf.data(), end - buf.data());
      }
    } else {
      long long val{};
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, 32> buf;
        if (auto [tp, tec] = std::to_chars(buf.data(), buf.data() + buf.size(), val); tec == std::errc()) {
          std::string_view sv(buf.data(), tp - buf.data());
          if (!sv.empty() && sv[0] == '-')
            sv.remove_prefix(1);
          str.assign(sv);
        }
      }
    }
    break;
  }
  case int_filter::hex: {
    long long val{};
    if (auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), val); ec == std::errc()) {
      std::array<char, 32> buf;
      auto [ptr, ec2] = std::to_chars(buf.data(), buf.data() + buf.size(), val, 16);
      str.assign(buf.data(), ptr - buf.data());
    }
    break;
  }
  case int_filter::oct: {
    long long val{};
    if (auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), val); ec == std::errc()) {
      std::array<char, 32> buf;
      auto [ptr, ec2] = std::to_chars(buf.data(), buf.data() + buf.size(), val, 8);
      str.assign(buf.data(), ptr - buf.data());
    }
    break;
  }
  case int_filter::bin: {
    long long val{};
    if (auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), val); ec == std::errc()) {
      std::array<char, 64> buf;
      auto [ptr, ec2] = std::to_chars(buf.data(), buf.data() + buf.size(), val, 2);
      if (ec2 == std::errc()) {
        str.assign(buf.data(), ptr - buf.data());
      }
    }
    break;
  }
  case int_filter::neg: {
    auto data = str.data();
    auto size = str.size();
    if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
      double val{};
      if (auto [p, ec] = fast_float::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, glz::zmij::double_buffer_size> buf;
        auto end = glz::to_chars(buf.data(), -val);
        str.assign(buf.data(), end - buf.data());
      }
    } else {
      long long val{};
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, 32> buf;
        if (auto [tp, tec] = std::to_chars(buf.data(), buf.data() + buf.size(), val); tec == std::errc()) {
          std::string_view sv(buf.data(), tp - buf.data());
          if (!sv.empty() && sv[0] == '-') {
            sv.remove_prefix(1);
            str.assign(sv);
          } else {
            str = std::string("-") + std::string(sv);
          }
        }
      }
    }
    break;
  }
  case int_filter::mod: {
    long long val{};
    if (auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), val); ec == std::errc()) {
      auto divisor = entry.arg;
      if (divisor == 0) {
        return std::unexpected(error_ctx{.ec = error_code::division_by_zero});
      }
      std::array<char, 32> buf;
      auto [tp, tec] = std::to_chars(buf.data(), buf.data() + buf.size(), val % divisor);
      if (tec == std::errc()) {
        str.assign(buf.data(), tp - buf.data());
      }
    }
    break;
  }
  case int_filter::numify: {
    auto data = str.data();
    auto size = str.size();
    if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
      double val{};
      if (auto [p, ec] = fast_float::from_chars(data, data + size, val); ec == std::errc()) {
        bool negative = val < 0;
        if (negative)
          val = -val;
        auto        int_part = static_cast<long long>(val);
        std::string num      = std::to_string(int_part);
        std::string result;
        {
          int digits = static_cast<int>(num.size());
          int groups = (digits - 1) / 3;
          result.resize(digits + groups, '\0');
          int out_pos = static_cast<int>(result.size()) - 1;
          int count   = 0;
          for (int i = digits - 1; i >= 0; --i) {
            result[out_pos--] = num[i];
            count++;
            if (count % 3 == 0 && i > 0) {
              result[out_pos--] = ',';
            }
          }
        }
        auto frac = val - static_cast<double>(int_part);
        if (frac != 0.0) {
          auto        dot_pos = str.find('.');
          std::size_t prec    = 6;
          if (dot_pos != std::string::npos) {
            prec = str.size() - dot_pos - 1;
            if (prec > 6)
              prec = 6;
            if (prec == 0)
              prec = 1;
          }
          std::array<char, 64> buf;
          auto [ptr, ec2] = std::to_chars(buf.data(), buf.data() + buf.size(), frac, std::chars_format::fixed, static_cast<int>(prec));
          if (ec2 == std::errc()) {
            auto frac_str = std::string_view(buf.data(), ptr - buf.data());
            if (frac_str.size() > 2) {
              frac_str = frac_str.substr(1);
            }
            result += frac_str;
          }
        }
        str = negative ? "-" + result : result;
      }
    } else {
      long long val{};
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        std::string num      = std::to_string(val);
        bool        negative = !num.empty() && num[0] == '-';
        if (negative)
          num.erase(0, 1);
        std::string result;
        {
          int digits = static_cast<int>(num.size());
          int groups = (digits - 1) / 3;
          result.resize(digits + groups, '\0');
          int out_pos = static_cast<int>(result.size()) - 1;
          int count   = 0;
          for (int i = digits - 1; i >= 0; --i) {
            result[out_pos--] = num[i];
            count++;
            if (count % 3 == 0 && i > 0) {
              result[out_pos--] = ',';
            }
          }
        }
        str = negative ? "-" + result : result;
      }
    }
    break;
  }
  case int_filter::is_neg: {
    auto data = str.data();
    auto size = str.size();
    if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
      double val{};
      if (auto [p, ec] = fast_float::from_chars(data, data + size, val); ec == std::errc()) {
        str = val < 0 ? "true" : "false";
      } else {
        long long val2{};
        if (auto [p2, ec2] = std::from_chars(data, data + size, val2); ec2 == std::errc()) {
          str = val2 < 0 ? "true" : "false";
        } else {
          str = "false";
        }
      }
    } else {
      long long val{};
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        str = val < 0 ? "true" : "false";
      } else {
        str = "false";
      }
    }
    break;
  }
  case int_filter::eq: {
    auto target = entry.arg;
    auto data   = str.data();
    auto size   = str.size();
    if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
      double val{};
      if (auto [p, ec] = fast_float::from_chars(data, data + size, val); ec == std::errc()) {
        str = (static_cast<long long>(val) == target) ? "true" : "false";
      } else {
        long long val2{};
        if (auto [p2, ec2] = std::from_chars(data, data + size, val2); ec2 == std::errc()) {
          str = (val2 == target) ? "true" : "false";
        } else {
          str = "false";
        }
      }
    } else {
      long long val{};
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        str = (val == target) ? "true" : "false";
      } else {
        str = "false";
      }
    }
    break;
  }

  case int_filter::ne:
  case int_filter::gt:
  case int_filter::gte:
  case int_filter::lt:
  case int_filter::lte: {
    auto target = static_cast<long long>(entry.arg);
    auto data   = str.data();
    auto size   = str.size();
    bool result = false;
    if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
      double val{};
      if (auto [p, ec] = fast_float::from_chars(data, data + size, val); ec == std::errc()) {
        double ftarget = static_cast<double>(entry.arg);
        switch (entry.filter) {
        case int_filter::ne:
          result = (val != ftarget);
          break;
        case int_filter::gt:
          result = (val > ftarget);
          break;
        case int_filter::gte:
          result = (val >= ftarget);
          break;
        case int_filter::lt:
          result = (val < ftarget);
          break;
        case int_filter::lte:
          result = (val <= ftarget);
          break;
        default:
          break;
        }
      } else {
        long long val2{};
        if (auto [p2, ec2] = std::from_chars(data, data + size, val2); ec2 == std::errc()) {
          switch (entry.filter) {
          case int_filter::ne:
            result = (val2 != target);
            break;
          case int_filter::gt:
            result = (val2 > target);
            break;
          case int_filter::gte:
            result = (val2 >= target);
            break;
          case int_filter::lt:
            result = (val2 < target);
            break;
          case int_filter::lte:
            result = (val2 <= target);
            break;
          default:
            break;
          }
        }
      }
    } else {
      long long val{};
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        switch (entry.filter) {
        case int_filter::ne:
          result = (val != target);
          break;
        case int_filter::gt:
          result = (val > target);
          break;
        case int_filter::gte:
          result = (val >= target);
          break;
        case int_filter::lt:
          result = (val < target);
          break;
        case int_filter::lte:
          result = (val <= target);
          break;
        default:
          break;
        }
      }
    }
    str = result ? "true" : "false";
    break;
  }
  case int_filter::add: {
    auto arg    = entry.arg;
    auto data   = str.data();
    auto size   = str.size();
    if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
      double val{};
      if (auto [p, ec] = fast_float::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, glz::zmij::double_buffer_size> buf;
        auto end = glz::to_chars(buf.data(), val + arg);
        str.assign(buf.data(), end - buf.data());
      }
    } else {
      long long val{};
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, 32> buf;
        if (auto [tp, tec] = std::to_chars(buf.data(), buf.data() + buf.size(), val + arg); tec == std::errc()) {
          str.assign(buf.data(), tp - buf.data());
        }
      }
    }
    break;
  }
  case int_filter::sub: {
    auto arg    = entry.arg;
    auto data   = str.data();
    auto size   = str.size();
    if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
      double val{};
      if (auto [p, ec] = fast_float::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, glz::zmij::double_buffer_size> buf;
        auto end = glz::to_chars(buf.data(), val - arg);
        str.assign(buf.data(), end - buf.data());
      }
    } else {
      long long val{};
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, 32> buf;
        if (auto [tp, tec] = std::to_chars(buf.data(), buf.data() + buf.size(), val - arg); tec == std::errc()) {
          str.assign(buf.data(), tp - buf.data());
        }
      }
    }
    break;
  }
  case int_filter::mul: {
    auto arg    = entry.arg;
    auto data   = str.data();
    auto size   = str.size();
    if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
      double val{};
      if (auto [p, ec] = fast_float::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, glz::zmij::double_buffer_size> buf;
        auto end = glz::to_chars(buf.data(), val * arg);
        str.assign(buf.data(), end - buf.data());
      }
    } else {
      long long val{};
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, 32> buf;
        if (auto [tp, tec] = std::to_chars(buf.data(), buf.data() + buf.size(), val * arg); tec == std::errc()) {
          str.assign(buf.data(), tp - buf.data());
        }
      }
    }
    break;
  }
  case int_filter::div: {
    auto arg    = entry.arg;
    auto data   = str.data();
    auto size   = str.size();
    if (arg == 0) {
      return std::unexpected(error_ctx{.ec = error_code::division_by_zero});
    }
    if (str.find('.') != std::string::npos || str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
      double val{};
      if (auto [p, ec] = fast_float::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, glz::zmij::double_buffer_size> buf;
        auto end = glz::to_chars(buf.data(), val / arg);
        str.assign(buf.data(), end - buf.data());
      }
    } else {
      long long val{};
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, 32> buf;
        if (auto [tp, tec] = std::to_chars(buf.data(), buf.data() + buf.size(), val / arg); tec == std::errc()) {
          str.assign(buf.data(), tp - buf.data());
        }
      }
    }
    break;
  }
  case int_filter::zerofill: {
    long long val{};
    if (auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), val); ec == std::errc()) {
      auto width    = entry.arg;
      auto s        = std::to_string(val);
      bool negative = !s.empty() && s[0] == '-';
      if (negative)
        s.erase(0, 1);
      auto digits = static_cast<int>(s.size());
      auto total  = negative ? digits + 1 : digits;
      if (total < width) {
        auto padding = width - total;
        if (negative) {
          str = "-" + std::string(padding, '0') + s;
        } else {
          str = std::string(padding, '0') + s;
        }
      }
    }
    break;
  }
  }
  return {};
}

/**
 * @brief 実数フィルタを適用する
 * @param str 対象の文字列
 * @param entry 適用するフィルタの種別と引数
 */
constexpr void apply_float_filter(std::string& str, float_filter_entry entry) {
  switch (entry.filter) {
  case float_filter::precision: {
    double val{};
    if (auto [p, ec] = fast_float::from_chars(str.data(), str.data() + str.size(), val); ec == std::errc()) {
      std::array<char, 64> buf;
      auto [ptr, ec2] = std::to_chars(buf.data(), buf.data() + buf.size(), val, std::chars_format::fixed, static_cast<int>(entry.arg));
      if (ec2 == std::errc()) {
        str.assign(buf.data(), ptr - buf.data());
      }
    }
    break;
  }
  }
}

}  // namespace injamm::detail
