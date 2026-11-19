#include <pugixml.hpp>
#include <cstdint>
#include <stdexcept>
#include <string_view>

std::uint64_t parse(std::string_view input) {
    pugi::xml_document document;
    const auto result = document.load_buffer(input.data(), input.size());
    if (!result) throw std::runtime_error(result.description());
    const auto root = document.document_element();
    std::uint64_t count = std::string_view(root.name()).size();
    for (auto child : root.children()) count += std::string_view(child.name()).size() + 1;
    return count;
}
