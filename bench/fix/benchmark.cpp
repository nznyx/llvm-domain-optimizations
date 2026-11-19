#include <hffix.hpp>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string_view>

std::uint64_t parse(std::string_view input) {
    // The FIX 4.1 text fixture has a trailing newline after its last SOH.
    if (input.ends_with('\n')) input.remove_suffix(1);
    std::uint64_t result = 0;
    do {
        if (input.size() < 16 || !input.starts_with("8=FIX"))
            throw std::runtime_error("invalid FIX prefix");
        hffix::message_reader message(input.data(), input.data() + input.size());
        if (!message.is_complete() || !message.is_valid())
            throw std::runtime_error("incomplete or invalid FIX message");
        const auto trailer = message.check_sum()->value();
        if (trailer.size() != 3 || std::string_view(trailer.begin() - 3, 3) != "10=" ||
            !std::all_of(trailer.begin(), trailer.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
            trailer.as_int<unsigned>() != message.calculate_check_sum())
            throw std::runtime_error("invalid FIX checksum");
        for (const auto &field : message)
            result += field.tag() + field.value().size();
        input.remove_prefix(message.message_end() - input.data());
    } while (!input.empty());
    return result;
}
