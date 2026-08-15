#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace m3d {

class ObjectId final {
public:
    constexpr ObjectId() noexcept = default;
    constexpr ObjectId(std::uint64_t high, std::uint64_t low) noexcept
        : high_(high), low_(low) {}

    [[nodiscard]] static ObjectId generate();
    [[nodiscard]] static constexpr ObjectId null() noexcept { return {}; }
    [[nodiscard]] static std::optional<ObjectId> fromString(const std::string& value);

    [[nodiscard]] std::string toString() const;
    [[nodiscard]] constexpr bool isNull() const noexcept { return high_ == 0 && low_ == 0; }
    [[nodiscard]] constexpr std::uint64_t high() const noexcept { return high_; }
    [[nodiscard]] constexpr std::uint64_t low() const noexcept { return low_; }

    friend constexpr bool operator==(const ObjectId&, const ObjectId&) noexcept = default;

private:
    std::uint64_t high_{0};
    std::uint64_t low_{0};
};

struct ObjectIdHash final {
    [[nodiscard]] std::size_t operator()(const ObjectId& value) const noexcept;
};

} // namespace m3d
