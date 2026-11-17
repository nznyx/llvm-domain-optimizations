#include <boost/system/error_code.hpp>
#include <iostream>

extern "C" {
__attribute__((noinline)) int scalar_error(int error) {
    if (error) return 7;
    return 3;
}
__attribute__((noinline)) int inverted_error(int error) {
    if (error == 0) return 3;
    return 7;
}
__attribute__((noinline)) int negated_error(int error) {
    if (!error) return 3;
    return 7;
}
__attribute__((noinline)) int ec_check(boost::system::error_code const& ec) {
    if (ec) return 7;
    return 3;
}
__attribute__((noinline)) int ec_negated(boost::system::error_code const& ec) {
    if (!ec) return 3;
    return 7;
}
__attribute__((noinline)) int get_error(int x) { return x; }
__attribute__((noinline)) int method_check(int x) {
    if (get_error(x)) return 7;
    return 3;
}
__attribute__((noinline)) int ordinary(int count) {
    if (count) return 7;
    return 3;
}
__attribute__((noinline)) int terror_check(int terror) {
    if (terror) return 7;
    return 3;
}
__attribute__((noinline)) int parse(int& error) { return error; }
__attribute__((noinline)) int unrelated_call(int x) {
    if (parse(x)) return 7;
    return 3;
}
__attribute__((noinline)) int compared_error(int value, int error) {
    if (value == error) return 7;
    return 3;
}
__attribute__((noinline)) int specific_error(int error) {
    if (error != 7) return 3;
    return 7;
}
__attribute__((noinline)) int pointer_error(int* error) {
    if (error != nullptr) return 7;
    return 3;
}
__attribute__((noinline)) int uppercase_error(int ERROR) {
    if (ERROR) return 7;
    return 3;
}
__attribute__((noinline)) int camel_error(int errorCode) {
    if (errorCode) return 7;
    return 3;
}
__attribute__((noinline)) int snake_error(int error_code) {
    if (error_code) return 7;
    return 3;
}
__attribute__((noinline)) int getError(int x) { return x; }
__attribute__((noinline)) int camel_method(int x) {
    if (getError(x)) return 7;
    return 3;
}
__attribute__((noinline)) int specific_method(int x) {
    if (get_error(x) == 7) return 7;
    return 3;
}
__attribute__((noinline)) int errorless_check(int errorless) {
    if (errorless) return 7;
    return 3;
}
__attribute__((noinline)) int relational(int error) {
    if (error < 5) return 7;
    return 3;
}
}
int main() {
    long sum = 0;
    for (int x = -100; x <= 100; ++x) {
        boost::system::error_code ec{x, boost::system::generic_category()};
        int expected = x ? 7 : 3;
        if (scalar_error(x) != expected || inverted_error(x) != expected ||
            negated_error(x) != expected || ec_check(ec) != expected ||
            ec_negated(ec) != expected || method_check(x) != expected ||
            compared_error(x, 7) != (x == 7 ? 7 : 3) ||
            specific_error(x) != (x == 7 ? 7 : 3) ||
            pointer_error(nullptr) != 3 || pointer_error(&x) != 7 ||
            uppercase_error(x) != expected || camel_error(x) != expected ||
            snake_error(x) != expected || camel_method(x) != expected ||
            specific_method(x) != (x == 7 ? 7 : 3))
            return 1;
        sum += ordinary(x) + terror_check(x) + unrelated_call(x) + relational(x);
    }
    std::cout << sum << '\n';
}
