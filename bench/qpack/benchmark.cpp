#include "qpack.h"
#include "qpack_decode.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace nghttp3 {
Config config{};
}

static std::uint64_t number(std::string_view &input, std::size_t bytes) {
    if (input.size() < bytes) throw std::runtime_error("truncated QPACK frame");
    std::uint64_t value = 0;
    for (unsigned char c : input.substr(0, bytes)) value = (value << 8) | c;
    input.remove_prefix(bytes);
    return value;
}

std::uint64_t parse(std::string_view input) {
    nghttp3::Decoder decoder(4096, 100);
    if (decoder.init()) throw std::runtime_error("QPACK allocation failed");

    std::uint64_t result = 0;
    auto consume = [&](const nghttp3::Headers &fields) {
        for (const auto &[name, value] : fields) {
            result += 1 + 17 * name.size() + 31 * value.size();
            if (!name.empty()) result += static_cast<unsigned char>(name.front());
            if (!value.empty()) result += static_cast<unsigned char>(value.back());
        }
    };
    while (!input.empty()) {
        auto id = number(input, 8), size = number(input, 4);
        if (id > std::numeric_limits<std::int64_t>::max() || !size || size > input.size())
            throw std::runtime_error("invalid QPACK frame");
        // The upstream example advances pointers in nghttp3_buf but only reads the bytes.
        auto *data = reinterpret_cast<std::uint8_t *>(const_cast<char *>(input.data()));
        nghttp3_buf wire;
        wire.begin = wire.pos = data;
        wire.end = wire.last = data + size;
        input.remove_prefix(size);
        if (id == 0) {
            if (decoder.read_encoder(&wire)) throw std::runtime_error("invalid QPACK encoder stream");
            while (decoder.get_num_blocked()) {
                auto [stream, fields, status] = decoder.process_blocked();
                if (status) throw std::runtime_error("invalid blocked QPACK stream");
                if (stream == -1) break;
                consume(fields);
            }
        } else {
            auto [fields, status] = decoder.read_request(&wire, static_cast<std::int64_t>(id));
            if (status < 0) throw std::runtime_error("invalid QPACK field section");
            if (status == 0) consume(fields);
        }
    }
    if (decoder.get_num_blocked()) throw std::runtime_error("missing QPACK table update");
    return result;
}
