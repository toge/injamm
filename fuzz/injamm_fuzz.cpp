#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "../include/injamm/escape_hatch.hpp"

// ファズ用のネスト構造アイテム（配列要素として使用）
struct FuzzItem {
  std::string name;
  int         value;
  double      rate;
  bool        active;
};

// ファズ用のルートデータ構造（各種型を含む）
struct FuzzData {
  std::string           title;
  int                   count;
  double                ratio;
  bool                  flag;
  std::vector<FuzzItem> items;
  std::string           extra;
};

template <>
struct glz::meta<FuzzItem> {
  using T = FuzzItem;
  static constexpr auto value =
      glz::object("name", &T::name, "value", &T::value, "rate", &T::rate, "active", &T::active);
};

template <>
struct glz::meta<FuzzData> {
  using T = FuzzData;
  static constexpr auto value = glz::object(
      "title", &T::title, "count", &T::count, "ratio", &T::ratio, "flag", &T::flag,
      "items", &T::items, "extra", &T::extra);
};

// ファズ入力として用いる固定のサンプルデータを構築する
static FuzzData makeFuzzData() {
  FuzzData d;
  d.title = "fuzz";
  d.count = 3;
  d.ratio = 1.5;
  d.flag  = true;
  d.items = {{"a", 1, 0.1, true}, {"b", 2, 0.2, false}, {"c", 3, 0.3, true}};
  d.extra = "x";
  return d;
}

// libFuzzer エントリポイント: 任意のバイト列をテンプレートとしてレンダリングしクラッシュを検出
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* raw_data, size_t size) {
  if (size == 0) return 0;

  std::string_view tmpl(reinterpret_cast<const char*>(raw_data), size);
  auto             data = makeFuzzData();

  auto engine = injamm::engine<FuzzData>(tmpl);
  (void)engine.render(data);

  return 0;
}

#ifdef INJAMM_FUZZ_STANDALONE
// 単一の入力バッファを libFuzzer エントリポイントへ渡して実行する
static int runOneInput(std::vector<uint8_t> const& buf) {
  return LLVMFuzzerTestOneInput(buf.data(), buf.size());
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: injamm_fuzz <file1> [file2...]\n");
    return 1;
  }

  for (int i = 1; i < argc; ++i) {
    std::string path(argv[i]);
    std::ifstream is(path, std::ios::binary | std::ios::ate);
    if (!is) {
      std::fprintf(stderr, "Cannot open: %s\n", path.c_str());
      return 1;
    }
    auto                sz = static_cast<std::size_t>(is.tellg());
    is.seekg(0);
    std::vector<uint8_t> buf(sz);
    is.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    auto actual = static_cast<std::size_t>(is.gcount());
    buf.resize(actual);
    if (runOneInput(buf) != 0) {
      std::fprintf(stderr, "FAIL: %s\n", path.c_str());
      return 1;
    }
  }
  return 0;
}
#endif
