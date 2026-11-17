#include <csv.h>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef CSV_TRAINING
extern "C" void __llvm_profile_reset_counters();
#endif

struct Results {
  uint64_t fields = 0, rows = 0, hash = 14695981039346656037ull;
  bool digest;

  void add(unsigned char byte) {
    if (digest) hash = (hash ^ byte) * 1099511628211ull;
  }
};

void field(void *ptr, size_t size, void *context) {
  auto &result = *static_cast<Results *>(context);
  ++result.fields;
  if (result.digest) {
    for (size_t i = 0; i < size; ++i) result.add(static_cast<unsigned char *>(ptr)[i]);
    result.add(0xff);
  }
}

void row(int delimiter, void *context) {
  auto &result = *static_cast<Results *>(context);
  ++result.rows;
  result.add(static_cast<unsigned char>(delimiter));
}

uint64_t customCalls = 0;
bool extraTerminator = false;
int customSpace(unsigned char ch) {
  ++customCalls;
  return ch == ' ' || ch == '\t';
}
int customTerm(unsigned char ch) {
  ++customCalls;
  return ch == '\r' || ch == '\n' || (extraTerminator && ch == ';');
}

int parse(std::string_view input, Results &result, bool custom) {
  csv_parser p;
  if (csv_init(&p, CSV_STRICT | CSV_STRICT_FINI) != 0) throw std::runtime_error("csv_init");
  auto cleanup = std::unique_ptr<csv_parser, decltype(&csv_free)>(&p, csv_free);
  if (custom) {
    csv_set_space_func(&p, customSpace);
    csv_set_term_func(&p, customTerm);
  }
  const size_t consumed = csv_parse(&p, input.data(), input.size(), field, row, &result);
  if (consumed != input.size() || csv_fini(&p, field, row, &result) != 0)
    return csv_error(&p);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "usage: libcsv-bench REPEATS FILE.csv [FILE.csv ...]\n";
    return 2;
  }
  int repeats = std::stoi(argv[1]);
  if (repeats < 1) return 2;
  std::vector<std::string> inputs;
  size_t bytes = 0;
  for (int i = 2; i < argc; ++i) {
    std::ifstream stream(argv[i], std::ios::binary);
    if (!stream) throw std::runtime_error(argv[i]);
    inputs.emplace_back(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    bytes += inputs.back().size();
  }
  Results validation{.digest = true};
  Results custom{.digest = true};
  for (const auto &input : inputs) {
    if (parse(input, validation, false) || parse(input, custom, true))
      throw std::runtime_error("invalid CSV fixture");
  }
  if (validation.hash != custom.hash || validation.fields != custom.fields ||
      validation.rows != custom.rows || customCalls == 0)
    throw std::runtime_error("fallback differs");
  for (bool useCustom : {false, true})
    if (parse("a,b\"x\n", custom, useCustom) != CSV_EPARSE)
      throw std::runtime_error("accepted malformed CSV");
  Results standardRows{.digest = true}, customRows{.digest = true};
  extraTerminator = true;
  if (parse("first;second", standardRows, false) || parse("first;second", customRows, true) ||
      standardRows.rows != 1 || customRows.rows != 2 || standardRows.hash == customRows.hash)
    throw std::runtime_error("custom terminator not applied");
  extraTerminator = false;
  Results timed{.digest = false};
#ifdef CSV_TRAINING
  __llvm_profile_reset_counters();
#endif
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < repeats; ++i)
    for (const auto &input : inputs)
      if (parse(input, timed, false)) throw std::runtime_error("timed parse failed");
  const auto end = std::chrono::steady_clock::now();
  if (timed.fields != validation.fields * repeats || timed.rows != validation.rows * repeats)
    throw std::runtime_error("timed parse output differs");
  std::cout << std::chrono::duration<double, std::milli>(end - start).count()
            << ' ' << timed.fields << ' ' << bytes << ' ' << validation.hash << '\n';
}
