#include "mobile3d/core/composite_command.hpp"

#include <utility>

namespace m3d {

CompositeCommand::CompositeCommand(std::string name)
    : name_(name.empty() ? "Composite Edit" : std::move(name)) {}

void CompositeCommand::add(std::unique_ptr<EditorCommand> command) {
    if (command) {
        commands_.push_back(std::move(command));
    }
}

bool CompositeCommand::execute() {
    if (commands_.empty()) {
        return false;
    }

    std::size_t executed = 0;
    for (; executed < commands_.size(); ++executed) {
        if (!commands_[executed]->execute()) {
            for (std::size_t rollback = executed; rollback > 0; --rollback) {
                (void)commands_[rollback - 1]->undo();
            }
            return false;
        }
    }
    return true;
}

bool CompositeCommand::undo() {
    if (commands_.empty()) {
        return false;
    }

    std::size_t undone = 0;
    for (std::size_t index = commands_.size(); index > 0; --index) {
        if (!commands_[index - 1]->undo()) {
            const auto firstRedone = index;
            for (std::size_t redoIndex = firstRedone; redoIndex < firstRedone + undone; ++redoIndex) {
                (void)commands_[redoIndex]->execute();
            }
            return false;
        }
        ++undone;
    }
    return true;
}

} // namespace m3d
