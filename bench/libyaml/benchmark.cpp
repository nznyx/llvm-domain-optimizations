#include <yaml.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct Parser {
  yaml_parser_t value;
  Parser() {
    if (!yaml_parser_initialize(&value))
      throw std::runtime_error("cannot initialize YAML parser");
  }
  ~Parser() { yaml_parser_delete(&value); }
};

struct Counts {
  uint64_t events = 0;
  uint64_t scalarBytes = 0;
};

Counts parse(std::string_view input) {
  Parser parser;
  yaml_parser_set_input_string(&parser.value,
                               reinterpret_cast<const unsigned char *>(input.data()),
                               input.size());
  Counts counts;
  while (true) {
    yaml_event_t event;
    if (!yaml_parser_parse(&parser.value, &event))
      throw std::runtime_error(parser.value.problem ? parser.value.problem : "YAML parse failed");
    ++counts.events;
    if (event.type == YAML_SCALAR_EVENT)
      counts.scalarBytes += event.data.scalar.length;
    const bool done = event.type == YAML_STREAM_END_EVENT;
    yaml_event_delete(&event);
    if (done) return counts;
  }
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "usage: libyaml-bench REPEATS FILE.yaml [FILE.yaml ...]\n";
    return 2;
  }
  try {
    const int repeats = std::stoi(argv[1]);
    if (repeats < 1) return 2;
    std::vector<std::string> inputs;
    for (int i = 2; i < argc; ++i) {
      std::ifstream stream(argv[i], std::ios::binary);
      if (!stream) throw std::runtime_error(argv[i]);
      inputs.emplace_back(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    Counts total;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i)
      for (const auto &input : inputs) {
        const auto counts = parse(input);
        total.events += counts.events;
        total.scalarBytes += counts.scalarBytes;
      }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    std::cout << std::chrono::duration<double, std::milli>(elapsed).count()
              << ' ' << total.events << ' ' << total.scalarBytes << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
