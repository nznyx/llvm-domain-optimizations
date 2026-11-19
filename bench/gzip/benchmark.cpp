#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

std::uint64_t parse(std::string_view input) {
    z_stream stream{};
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK)
        throw std::runtime_error("inflateInit2 failed");
    std::array<Bytef, 32768> output;
    int status;
    do {
        if (!stream.avail_in && !input.empty()) {
            stream.avail_in = std::min<std::size_t>(input.size(), std::numeric_limits<uInt>::max());
            stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
            input.remove_prefix(stream.avail_in);
        }
        stream.avail_out = output.size();
        stream.next_out = output.data();
        status = inflate(&stream, Z_NO_FLUSH);
    } while (status == Z_OK);
    const auto bytes = stream.total_out;
    const auto checksum = stream.adler; // zlib verifies the member's CRC32 and length.
    const bool complete = status == Z_STREAM_END && !stream.avail_in && input.empty();
    inflateEnd(&stream);
    if (!complete) throw std::runtime_error("expected one complete gzip member");
    return (static_cast<std::uint64_t>(checksum) << 32) ^ bytes;
}
