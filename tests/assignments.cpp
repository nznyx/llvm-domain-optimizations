#include <boost/beast/core/detail/config.hpp>
#include <boost/beast/http/error.hpp>
#include <iostream>
using boost::beast::http::error;
using boost::system::error_code;
extern "C" {
__attribute__((noinline)) int assigns_bad(int n, error_code& out) {
    if (n > 16) { BOOST_BEAST_ASSIGN_EC(out, error::bad_method); return 7; }
    return 3;
}
__attribute__((noinline)) int assigns_else(int n, error_code& out) {
    if (n <= 16) return 3;
    BOOST_BEAST_ASSIGN_EC(out, error::body_limit);
    return 7;
}
__attribute__((noinline)) int assigns_direct(int n, error_code& out) {
    if (n > 16) { out = error::bad_version; return 7; }
    return 3;
}
__attribute__((noinline)) int assigns_service(int n, error_code& out) {
    if (n > 16) { BOOST_BEAST_ASSIGN_EC(out, error::need_more); return 7; }
    return 3;
}
__attribute__((noinline)) int assigns_buffer(int n, error_code& out) {
    if (n > 16) { out = error::need_buffer; return 7; }
    return 3;
}
__attribute__((noinline)) int assigns_chunk(int n, error_code& out) {
    if (n > 16) { out = error::end_of_chunk; return 7; }
    return 3;
}
__attribute__((noinline)) int assigns_eof(int n, error_code& out) {
    if (n > 16) { out = error::end_of_stream; return 7; }
    return 3;
}
__attribute__((noinline)) int assigns_clear(int n, error_code& out) {
    if (n > 16) { out = error::bad_method; out.clear(); return 7; }
    return 3;
}
__attribute__((noinline)) int assigns_shared(int n, error_code& out) {
    int result = n > 16 ? 7 : 3;
    out = error::bad_method;
    return result;
}
__attribute__((noinline)) int constructs_only(int n) {
    if (n > 16) { error_code out = error::bad_method; return out.value(); }
    return 3;
}
__attribute__((noinline)) int assigns_unknown(int n, int code, error_code& out) {
    if (n > 16) { out = static_cast<error>(code); return 7; }
    return 3;
}
__attribute__((noinline)) int assigns_both(int n, error_code& out) {
    if (n > 16) { out = error::bad_method; return 7; }
    out = error::bad_version;
    return 3;
}
}
int main() {
    using Fn = int (*)(int, error_code&);
    Fn fns[] = {assigns_bad, assigns_else, assigns_direct, assigns_service,
        assigns_buffer, assigns_chunk, assigns_eof, assigns_clear, assigns_both};
    for (int n = -100; n <= 100; ++n) {
        for (auto f : fns) {
            error_code out;
            if (f(n, out) != (n > 16 ? 7 : 3)) return 1;
            std::cout << out.value() << ':';
        }
        error_code out;
        std::cout << assigns_shared(n, out) << ':' << out.value() << ':'
                  << constructs_only(n) << ':' << assigns_unknown(n, 12, out)
                  << ':' << out.value() << '\n';
    }
}
