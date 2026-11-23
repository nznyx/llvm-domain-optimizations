#include <json11.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>

std::uint64_t parse(const std::string &input) {
    std::string error;
    auto doc = json11::Json::parse(input, error);
    if (!error.empty()) throw std::runtime_error(error);
    return 1 + doc.array_items().size() + doc.object_items().size();
}
