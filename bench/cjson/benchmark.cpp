#include <cJSON.h>
#include <cstdint>
#include <stdexcept>
#include <string_view>

std::uint64_t parse(std::string_view input) {
    const char *end = nullptr;
    cJSON *doc = cJSON_ParseWithLengthOpts(input.data(), input.size() + 1, &end, true);
    if (!doc) throw std::runtime_error("invalid JSON");
    const auto size = cJSON_GetArraySize(doc);
    cJSON_Delete(doc);
    return 1 + static_cast<std::uint64_t>(size);
}
