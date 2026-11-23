#include <climits>
#include <cstdint>
#include <stdexcept>
#include <string_view>

extern "C" bool scan_tokens(const char* input, int size, std::uint64_t* digest);

std::uint64_t parse(std::string_view input) {
    if (input.size() > INT_MAX - 2 || input.find('\0') != input.npos)
        throw std::runtime_error("expected a text file fitting the scanner buffer");
    std::uint64_t digest = 0;
    if (!scan_tokens(input.data(), static_cast<int>(input.size()), &digest))
        throw std::runtime_error("lexer rejected the input");
    return digest;
}
