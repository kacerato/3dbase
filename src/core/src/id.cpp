#include "mobile3d/core/id.hpp"

#include <array>
#include <charconv>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>
#include <string_view>

namespace m3d {
namespace {

std::uint64_t random64() {
    thread_local std::mt19937_64 generator([] {
        std::array<std::uint32_t, 8> seedData{};
        std::random_device device;
        for (auto& value : seedData) {
            value = device();
        }
        std::seed_seq seed(seedData.begin(), seedData.end());
        return std::mt19937_64(seed);
    }());
    return generator();
}

std::optional<std::uint64_t> parseHex64(std::string_view value) {
    std::uint64_t result = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto parsed = std::from_chars(begin, end, result, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> parseId(const std::string& value) {
    if (value.size() != 32) {
        return std::nullopt;
    }
    const auto high = parseHex64(std::string_view(value).substr(0, 16));
    const auto low = parseHex64(std::string_view(value).substr(16, 16));
    if (!high || !low) {
        return std::nullopt;
    }
    return std::pair{*high, *low};
}

std::string formatId(std::uint64_t high, std::uint64_t low) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
           << std::setw(16) << high
           << std::setw(16) << low;
    return stream.str();
}

std::size_t hashId(std::uint64_t high, std::uint64_t low) noexcept {
    const auto h1 = std::hash<std::uint64_t>{}(high);
    const auto h2 = std::hash<std::uint64_t>{}(low);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
}

} // namespace

ObjectId ObjectId::generate() {
    ObjectId id;
    do {
        id = ObjectId(random64(), random64());
    } while (id.isNull());
    return id;
}

std::optional<ObjectId> ObjectId::fromString(const std::string& value) {
    const auto parsed = parseId(value);
    return parsed ? std::optional<ObjectId>{ObjectId(parsed->first, parsed->second)} : std::nullopt;
}

std::string ObjectId::toString() const {
    return formatId(high_, low_);
}

std::size_t ObjectIdHash::operator()(const ObjectId& value) const noexcept {
    return hashId(value.high(), value.low());
}

ResourceId ResourceId::generate() {
    ResourceId id;
    do {
        id = ResourceId(random64(), random64());
    } while (id.isNull());
    return id;
}

std::optional<ResourceId> ResourceId::fromString(const std::string& value) {
    const auto parsed = parseId(value);
    return parsed ? std::optional<ResourceId>{ResourceId(parsed->first, parsed->second)} : std::nullopt;
}

std::string ResourceId::toString() const {
    return formatId(high_, low_);
}

std::size_t ResourceIdHash::operator()(const ResourceId& value) const noexcept {
    return hashId(value.high(), value.low());
}

} // namespace m3d
