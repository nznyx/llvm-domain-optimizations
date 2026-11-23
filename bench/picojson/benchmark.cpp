#include <picojson.h>
#include <cstdint>
#include <stdexcept>
#include <string_view>

std::uint64_t parse(std::string_view input) {
    picojson::value doc;
    std::string error;
    auto end = picojson::parse(doc, input.begin(), input.end(), &error);
    if (!error.empty() || input.substr(end - input.begin()).find_first_not_of(" \r\t\n") != input.npos)
        throw std::runtime_error("invalid JSON");
    if (doc.is<picojson::object>()) return 1 + doc.get<picojson::object>().size();
    if (doc.is<picojson::array>()) return 1 + doc.get<picojson::array>().size();
    return 1;
}
