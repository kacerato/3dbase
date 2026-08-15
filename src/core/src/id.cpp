#include "mobile3d/core/id.hpp"

#include <array>
#include <charconv>
#include <iomanip>
#include <random>
#include <sstream>

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

} // namespace

ObjectId ObjectId::generate() {
    ObjectId id;
    do {
        id = ObjectId(random64(), random64());
    } while (id.isNull());
    return id;
}

std::optional<ObjectId> ObjectId::fromString(const std::string& value) {
    if (value.size() != 32) {
        return std::nullopt;
    }

    const auto high = parseHex64(std::string_view(value).substr(0, 16));
    const auto low = parseHex64(std::string_view(value).substr(16, 16));
    if (!high || !low) {
        return std::nullopt;
    }
    return ObjectId(*high, *low);
}

std::string ObjectId::toString() const {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
           << std::setw(16) << high_
           << std::setw(16) << low_;
    return stream.str();
}

std::size_t ObjectIdHash::operator()(const ObjectId& value) const noexcept {
    const auto h1 = std::hash<std::uint64_t>{}(value.high());
    const auto h2 = std::hash<std::uint64_t>{}(value.low());
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
}

} // namespace m3d
