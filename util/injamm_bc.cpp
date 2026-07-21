#include "injamm.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>

namespace {

struct CliContext {
  int _dummy{};
};

void print_usage() {
  std::cout << "Usage: injamm_bc -i <input> -o <output.bin> [options]\n"
            << "  -i, --input   <path>    Template file (-: stdin)\n"
            << "  -e, --expr    <string>  Inline template string\n"
            << "  -o, --output  <path>    Output bytecode file\n"
            << "  -D, --define  <k>=<v>   Define @var constant (repeatable)\n"
            << "  -h, --help              Show this help\n";
}

} // namespace

template <>
struct glz::meta<CliContext> {
  static constexpr auto value = glz::object("_dummy", &CliContext::_dummy);
};

int main(int argc, char* argv[]) {
  std::string input_path;
  std::string output_path;
  std::string expr_tmpl;
  std::map<std::string, std::string, std::less<>> consts;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "-i" || arg == "--input") {
      if (++i >= argc) { std::cerr << "error: -i requires an argument\n"; return EXIT_FAILURE; }
      input_path = argv[i];
    } else if (arg == "-e" || arg == "--expr") {
      if (++i >= argc) { std::cerr << "error: -e requires an argument\n"; return EXIT_FAILURE; }
      expr_tmpl = argv[i];
    } else if (arg == "-o" || arg == "--output") {
      if (++i >= argc) { std::cerr << "error: -o requires an argument\n"; return EXIT_FAILURE; }
      output_path = argv[i];
    } else if (arg == "-D" || arg == "--define") {
      if (++i >= argc) { std::cerr << "error: -D requires key=value\n"; return EXIT_FAILURE; }
      std::string_view kv{argv[i]};
      auto eq = kv.find('=');
      if (eq == std::string_view::npos || eq == 0 || eq == kv.size() - 1) {
        std::cerr << "error: -D must be key=value, got: " << kv << "\n";
        return EXIT_FAILURE;
      }
      consts[std::string{kv.substr(0, eq)}] = std::string{kv.substr(eq + 1)};
    } else if (arg == "-h" || arg == "--help") {
      print_usage();
      return EXIT_SUCCESS;
    } else {
      std::cerr << "error: unknown argument: " << arg << "\n";
      print_usage();
      return EXIT_FAILURE;
    }
  }

  if (output_path.empty()) {
    std::cerr << "error: -o <output> is required\n";
    print_usage();
    return EXIT_FAILURE;
  }

  std::string tmpl;
  if (!expr_tmpl.empty()) {
    tmpl = std::move(expr_tmpl);
  } else if (input_path.empty()) {
    std::cerr << "error: -i <input> or -e <string> is required\n";
    print_usage();
    return EXIT_FAILURE;
  } else if (input_path == "-") {
    tmpl.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
  } else {
    std::ifstream ifs(input_path, std::ios::binary);
    if (!ifs) {
      std::cerr << "error: cannot open input file: " << input_path << "\n";
      return EXIT_FAILURE;
    }
    tmpl.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
  }

  injamm::engine<CliContext> eng = consts.empty()
    ? injamm::engine<CliContext>(tmpl)
    : injamm::engine<CliContext>(tmpl, consts);

  auto const& bc = eng.get_bytecode();
  if (bc.error.has_error()) {
    std::cerr << "error: " << bc.error.message() << "\n";
    return EXIT_FAILURE;
  }

  std::ofstream ofs(output_path, std::ios::binary);
  if (!ofs) {
    std::cerr << "error: cannot open output file: " << output_path << "\n";
    return EXIT_FAILURE;
  }
  auto ec = injamm::save_bytecode(bc, ofs);
  if (ec != injamm::error_code::none) {
    std::cerr << "error: failed to write bytecode\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
