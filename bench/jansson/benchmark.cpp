#include <jansson.h>
#include <cstdint>
#include <stdexcept>
#include <string_view>

std::uint64_t parse(std::string_view input) {
    json_object_seed(1);
    json_error_t error;
    auto *doc = json_loadb(input.data(), input.size(), JSON_DECODE_ANY, &error);
    if (!doc) throw std::runtime_error(error.text);
    const auto size = json_array_size(doc) + json_object_size(doc);
    json_decref(doc);
    return 1 + size;
}
