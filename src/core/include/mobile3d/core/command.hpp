#pragma once

#include <string_view>

namespace m3d {

class EditorCommand {
public:
    virtual ~EditorCommand() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool execute() = 0;
    [[nodiscard]] virtual bool undo() = 0;
};

} // namespace m3d
