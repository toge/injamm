#include <iostream>
#include <injamm.hpp>
#include <injamm/escape_hatch.hpp>

struct Data { int age = 30; };
template <> struct glz::meta<Data> { static constexpr auto value = glz::object("age", &Data::age); };

int main() {
  Data d;
  constexpr auto kTmpl = injamm::fixed_string("{{#if age > 20}}adult{{else}}minor{{/if}}");
  auto r = injamm::render<kTmpl>(d);
  std::cout << "age > 20: " << *r << "\n";
  return (*r == "adult") ? 0 : 1;
}
