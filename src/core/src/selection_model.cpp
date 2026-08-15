#include "mobile3d/core/selection_model.hpp"

#include "mobile3d/core/scene.hpp"

#include <algorithm>

namespace m3d {

bool SelectionModel::select(const Scene& scene, ObjectId id, SelectionMode mode) {
    if (!scene.contains(id)) {
        return false;
    }

    const auto it = std::find(selected_.begin(), selected_.end(), id);
    const bool alreadySelected = it != selected_.end();

    switch (mode) {
    case SelectionMode::Replace:
        selected_.assign(1, id);
        active_ = id;
        return true;
    case SelectionMode::Add:
        if (!alreadySelected) {
            selected_.push_back(id);
        }
        active_ = id;
        return true;
    case SelectionMode::Toggle:
        if (alreadySelected) {
            selected_.erase(it);
            if (active_ == id) {
                active_ = selected_.empty() ? std::nullopt
                                            : std::optional<ObjectId>{selected_.back()};
            }
        } else {
            selected_.push_back(id);
            active_ = id;
        }
        return true;
    }
    return false;
}

bool SelectionModel::remove(ObjectId id) {
    const auto it = std::find(selected_.begin(), selected_.end(), id);
    if (it == selected_.end()) {
        return false;
    }
    selected_.erase(it);
    if (active_ == id) {
        active_ = selected_.empty() ? std::nullopt : std::optional<ObjectId>{selected_.back()};
    }
    return true;
}

void SelectionModel::clear() noexcept {
    selected_.clear();
    active_.reset();
}

void SelectionModel::prune(const Scene& scene) {
    selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                   [&scene](ObjectId id) { return !scene.contains(id); }),
                    selected_.end());
    if (active_ && !scene.contains(*active_)) {
        active_ = selected_.empty() ? std::nullopt : std::optional<ObjectId>{selected_.back()};
    }
}

bool SelectionModel::contains(ObjectId id) const noexcept {
    return std::find(selected_.begin(), selected_.end(), id) != selected_.end();
}

} // namespace m3d
