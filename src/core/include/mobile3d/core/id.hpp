#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace m3d {

class ObjectId final {
public:
    constexpr ObjectId() noexcept = default;
    constexpr ObjectId(std::uint64_t high, std::uint64_t low) noexcept : high_(high), low_(low) {}
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
struct ObjectIdHash final { [[nodiscard]] std::size_t operator()(const ObjectId& value) const noexcept; };

class ResourceId final {
public:
    constexpr ResourceId() noexcept = default;
    constexpr ResourceId(std::uint64_t high, std::uint64_t low) noexcept : high_(high), low_(low) {}
    [[nodiscard]] static ResourceId generate();
    [[nodiscard]] static constexpr ResourceId null() noexcept { return {}; }
    [[nodiscard]] static std::optional<ResourceId> fromString(const std::string& value);
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] constexpr bool isNull() const noexcept { return high_ == 0 && low_ == 0; }
    [[nodiscard]] constexpr std::uint64_t high() const noexcept { return high_; }
    [[nodiscard]] constexpr std::uint64_t low() const noexcept { return low_; }
    friend constexpr bool operator==(const ResourceId&, const ResourceId&) noexcept = default;
private:
    std::uint64_t high_{0};
    std::uint64_t low_{0};
};
struct ResourceIdHash final { [[nodiscard]] std::size_t operator()(const ResourceId& value) const noexcept; };

class CollectionId final {
public:
    constexpr CollectionId() noexcept = default;
    constexpr CollectionId(std::uint64_t high, std::uint64_t low) noexcept : high_(high), low_(low) {}
    [[nodiscard]] static CollectionId generate();
    [[nodiscard]] static constexpr CollectionId null() noexcept { return {}; }
    [[nodiscard]] static std::optional<CollectionId> fromString(const std::string& value);
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] constexpr bool isNull() const noexcept { return high_ == 0 && low_ == 0; }
    [[nodiscard]] constexpr std::uint64_t high() const noexcept { return high_; }
    [[nodiscard]] constexpr std::uint64_t low() const noexcept { return low_; }
    friend constexpr bool operator==(const CollectionId&, const CollectionId&) noexcept = default;
private:
    std::uint64_t high_{0};
    std::uint64_t low_{0};
};
struct CollectionIdHash final { [[nodiscard]] std::size_t operator()(const CollectionId& value) const noexcept; };

class LayerId final {
public:
    constexpr LayerId() noexcept = default;
    constexpr LayerId(std::uint64_t high, std::uint64_t low) noexcept : high_(high), low_(low) {}
    [[nodiscard]] static LayerId generate();
    [[nodiscard]] static constexpr LayerId null() noexcept { return {}; }
    [[nodiscard]] static std::optional<LayerId> fromString(const std::string& value);
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] constexpr bool isNull() const noexcept { return high_ == 0 && low_ == 0; }
    [[nodiscard]] constexpr std::uint64_t high() const noexcept { return high_; }
    [[nodiscard]] constexpr std::uint64_t low() const noexcept { return low_; }
    friend constexpr bool operator==(const LayerId&, const LayerId&) noexcept = default;
private:
    std::uint64_t high_{0};
    std::uint64_t low_{0};
};
struct LayerIdHash final { [[nodiscard]] std::size_t operator()(const LayerId& value) const noexcept; };

} // namespace m3d
