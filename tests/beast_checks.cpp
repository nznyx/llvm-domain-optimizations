#include <boost/beast/http.hpp>
#include <boost/asio/buffer.hpp>
#include <iostream>
#include <string>

int main() {
    struct Case { std::string wire; bool valid; };
    Case cases[] = {
        {"GET / HTTP/1.1\r\nHost: example.org\r\n\r\n", true},
        {"POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\ntest", true},
        {"POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n4\r\ntest\r\n0\r\n\r\n", true},
        {"G@T / HTTP/1.1\r\n\r\n", false},
        {"GET / HTTP/x.y\r\n\r\n", false},
        {"POST / HTTP/1.1\r\nContent-Length: xyz\r\n\r\n", false},
        {"POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\na", false},
        {"GET / HTTP/1.1\r\nBad Header: x\r\n\r\n", false},
        {"POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\nz\r\nx\r\n0\r\n\r\n", false},
    };
    for (auto const& c : cases) {
        boost::beast::http::request_parser<boost::beast::http::string_body> parser;
        parser.eager(true);
        boost::system::error_code ec;
        auto used = parser.put(boost::asio::buffer(c.wire), ec);
        if (!ec && !parser.is_done()) parser.put_eof(ec);
        if ((!ec && parser.is_done()) != c.valid) return 1;
        std::cout << used << ':' << ec.value() << ':' << parser.is_done()
                  << ':' << parser.get().body() << '\n';
    }
}
