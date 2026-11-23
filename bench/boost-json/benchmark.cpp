#include <boost/json/src.hpp>
#include <cstdint>
#include <string_view>

std::uint64_t parse(std::string_view input) {
    auto doc = boost::json::parse(input);
    if (doc.is_object()) return 1 + doc.as_object().size();
    if (doc.is_array()) return 1 + doc.as_array().size();
    return 1;
}
