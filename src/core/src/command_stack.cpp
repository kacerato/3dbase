#include "mobile3d/core/command_stack.hpp"

#include <utility>

namespace m3d {

bool CommandStack::execute(std::unique_ptr<EditorCommand> command) {
    if (!command || !command->execute()) {
        return false;
    }

    Entry entry;
    entry.command = std::move(command);
    entry.beforeState = currentState_;
    entry.afterState = nextState_++;

    currentState_ = entry.afterState;
    redo_.clear();
    undo_.push_back(std::move(entry));
    trimUndoHistory();
    return true;
}

bool CommandStack::undo() {
    if (undo_.empty()) {
        return false;
    }

    Entry entry = std::move(undo_.back());
    undo_.pop_back();
    if (!entry.command->undo()) {
        undo_.push_back(std::move(entry));
        return false;
    }

    currentState_ = entry.beforeState;
    redo_.push_back(std::move(entry));
    return true;
}

bool CommandStack::redo() {
    if (redo_.empty()) {
        return false;
    }

    Entry entry = std::move(redo_.back());
    redo_.pop_back();
    if (!entry.command->execute()) {
        redo_.push_back(std::move(entry));
        return false;
    }

    currentState_ = entry.afterState;
    undo_.push_back(std::move(entry));
    trimUndoHistory();
    return true;
}

void CommandStack::clear() noexcept {
    undo_.clear();
    redo_.clear();
    currentState_ = nextState_++;
    savedState_ = currentState_;
}

std::string_view CommandStack::nextUndoName() const noexcept {
    return undo_.empty() ? std::string_view{} : undo_.back().command->name();
}

std::string_view CommandStack::nextRedoName() const noexcept {
    return redo_.empty() ? std::string_view{} : redo_.back().command->name();
}

void CommandStack::trimUndoHistory() {
    if (maxDepth_ == 0) {
        undo_.clear();
        return;
    }
    if (undo_.size() > maxDepth_) {
        const auto excess = undo_.size() - maxDepth_;
        undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(excess));
    }
}

} // namespace m3d
