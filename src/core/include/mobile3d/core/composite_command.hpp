#pragma once

#include "mobile3d/core/command.hpp"

#include <memory>
#include <string>
#include <vector>

namespace m3d {

class CompositeCommand final : public EditorCommand {
public:
    explicit CompositeCommand(std::string name);

    void add(std::unique_ptr<EditorCommand> command);
    [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;

private:
    std::string name_;
    std::vector<std::unique_ptr<EditorCommand>> commands_;
};

} // namespace m3d
