#include <boost/asio/buffer.hpp>
#include <boost/beast/http.hpp>
#include <cstdint>
#include <stdexcept>
#include <string_view>

std::uint64_t parse(std::string_view input) {
    namespace http = boost::beast::http;
    std::uint64_t result = 0;
    while (!input.empty()) {
        http::request_parser<http::empty_body> parser;
        parser.skip(true); // QIF carries headers only.
        boost::system::error_code ec;
        auto consumed = parser.put(boost::asio::buffer(input.data(), input.size()), ec);
        if (ec || !consumed || !parser.is_done())
            throw std::runtime_error(ec ? ec.message() : "incomplete HTTP headers");
        result += parser.get().target().size() + parser.get().version();
        for (const auto &field : parser.get())
            result += field.name_string().size() + field.value().size();
        input.remove_prefix(consumed);
    }
    return result;
}
