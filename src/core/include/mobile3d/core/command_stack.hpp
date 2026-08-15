#pragma once

#include "mobile3d/core/command.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace m3d {

class CommandStack final {
public:
    explicit CommandStack(std::size_t maxDepth = 256) : maxDepth_(maxDepth) {}

    [[nodiscard]] bool execute(std::unique_ptr<EditorCommand> command);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    void clear() noexcept;
    void markSaved() noexcept { savedState_ = currentState_; }

    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] bool isDirty() const noexcept { return currentState_ != savedState_; }
    [[nodiscard]] std::size_t undoCount() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t redoCount() const noexcept { return redo_.size(); }
    [[nodiscard]] std::string_view nextUndoName() const noexcept;
    [[nodiscard]] std::string_view nextRedoName() const noexcept;

private:
    struct Entry final {
        std::unique_ptr<EditorCommand> command;
        std::uint64_t beforeState{0};
        std::uint64_t afterState{0};
    };

    void trimUndoHistory();

    std::size_t maxDepth_{256};
    std::uint64_t nextState_{1};
    std::uint64_t currentState_{0};
    std::uint64_t savedState_{0};
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
};

} // namespace m3d
