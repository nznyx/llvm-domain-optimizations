#include <parson.h>
#include <cstdint>
#include <stdexcept>
#include <string_view>

std::uint64_t parse(std::string_view input) {
    JSON_Value *doc = json_parse_string(input.data());
    if (!doc) throw std::runtime_error("invalid JSON");
    const auto size = json_object_get_count(json_value_get_object(doc))
                    + json_array_get_count(json_value_get_array(doc));
    json_value_free(doc);
    return 1 + size;
}
