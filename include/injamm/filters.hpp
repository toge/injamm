#pragma once

#include "bytecode.hpp"
#include "types.hpp"
#include <array>
#include <charconv>
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
      str      = str.substr(start, end - start + 1);
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
      str = str.substr(0, max_len - 3) + "...";
    } else if (str.size() > static_cast<std::size_t>(max_len)) {
      str = str.substr(0, max_len);
    }
    break;
  }
  case string_filter::substr: {
    auto start  = entry.arg1;
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
  case string_filter::replace: {
    /** 単一文字置換: str 中の全 '\n' を ' ' に置換（シンプル実装） */
    for (auto& c : str) {
      if (c == '\n') c = ' ';
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
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, 64> buf;
        if (auto [tp, tec] = std::to_chars(buf.data(), buf.data() + buf.size(), std::abs(val)); tec == std::errc()) {
          str.assign(buf.data(), tp - buf.data());
        }
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
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
        std::array<char, 64> buf;
        if (auto [tp, tec] = std::to_chars(buf.data(), buf.data() + buf.size(), -val); tec == std::errc()) {
          str.assign(buf.data(), tp - buf.data());
        }
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
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
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
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
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
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
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
      if (auto [p, ec] = std::from_chars(data, data + size, val); ec == std::errc()) {
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
    if (auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), val); ec == std::errc()) {
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
