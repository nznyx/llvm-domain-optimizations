#include <nghttp2/nghttp2.h>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>

std::uint64_t parse(std::string_view input) {
    nghttp2_hd_inflater *raw = nullptr;
    if (nghttp2_hd_inflate_new(&raw)) throw std::runtime_error("HPACK allocation failed");
    std::unique_ptr<nghttp2_hd_inflater, decltype(&nghttp2_hd_inflate_del)>
        decoder(raw, &nghttp2_hd_inflate_del);

    std::uint64_t result = 0;
    while (!input.empty()) {
        if (input.size() < 4) throw std::runtime_error("truncated HPACK frame");
        std::uint32_t size = 0;
        for (unsigned char c : input.substr(0, 4)) size = (size << 8) | c;
        input.remove_prefix(4);
        if (size > input.size()) throw std::runtime_error("truncated HPACK block");
        auto wire = input.substr(0, size);
        input.remove_prefix(size);
        while (true) {
            nghttp2_nv field{};
            int flags = 0;
            auto n = nghttp2_hd_inflate_hd2(decoder.get(), &field, &flags,
                reinterpret_cast<const std::uint8_t *>(wire.data()), wire.size(), 1);
            if (n < 0) throw std::runtime_error("invalid HPACK block");
            wire.remove_prefix(static_cast<std::size_t>(n));
            if (flags & NGHTTP2_HD_INFLATE_EMIT) {
                result += 1 + 17 * field.namelen + 31 * field.valuelen;
                if (field.namelen) result += field.name[0];
                if (field.valuelen) result += field.value[field.valuelen - 1];
            }
            if (flags & NGHTTP2_HD_INFLATE_FINAL) break;
            if (!n && !(flags & NGHTTP2_HD_INFLATE_EMIT))
                throw std::runtime_error("incomplete HPACK block");
        }
        if (!wire.empty() || nghttp2_hd_inflate_end_headers(decoder.get()))
            throw std::runtime_error("unfinished HPACK block");
    }
    return result;
}
