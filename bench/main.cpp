#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

// json11 takes a std::string by reference. Keep its input copy outside the timer.
#ifdef BENCH_STRING_INPUT
std::uint64_t parse(const std::string &input);
#else
std::uint64_t parse(std::string_view input);
#endif

int main(int argc, char **argv) try {
    if (argc < 3) throw std::runtime_error("usage: BENCH REPEATS FILE [FILE ...]");
    const int repeats = std::stoi(argv[1]);
    if (repeats < 1) throw std::runtime_error("expected a positive repeat count");

    double milliseconds = 0;
    std::uint64_t total = 0;
    for (int i = 2; i < argc; ++i) {
        std::ifstream file(argv[i], std::ios::binary);
        if (!file) throw std::runtime_error(argv[i]);
        std::string input{std::istreambuf_iterator<char>(file), {}};
        if (input.empty()) throw std::runtime_error("empty input");
        const auto expected = parse(input);
        std::uint64_t sum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int r = 0; r < repeats; ++r) {
            // Full LTO can otherwise hoist a pure replay of the same buffer.
            asm volatile("" ::: "memory");
            sum += parse(input);
        }
        milliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (sum != expected * repeats) throw std::runtime_error("inconsistent parse");
        total += sum;
    }
    std::cout << milliseconds << ' ' << total << '\n';
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
