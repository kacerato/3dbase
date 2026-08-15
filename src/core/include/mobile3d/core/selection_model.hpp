#pragma once

#include "mobile3d/core/id.hpp"

#include <optional>
#include <vector>

namespace m3d {

class Scene;

enum class SelectionMode {
    Replace,
    Add,
    Toggle,
};

class SelectionModel final {
public:
    [[nodiscard]] bool select(const Scene& scene, ObjectId id,
                              SelectionMode mode = SelectionMode::Replace);
    [[nodiscard]] bool remove(ObjectId id);
    void clear() noexcept;
    void prune(const Scene& scene);

    [[nodiscard]] bool contains(ObjectId id) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return selected_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return selected_.size(); }
    [[nodiscard]] const std::vector<ObjectId>& selected() const noexcept { return selected_; }
    [[nodiscard]] std::optional<ObjectId> active() const noexcept { return active_; }

private:
    std::vector<ObjectId> selected_;
    std::optional<ObjectId> active_;
};

} // namespace m3d
