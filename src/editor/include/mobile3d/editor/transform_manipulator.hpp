#pragma once

#include "mobile3d/editor/editor_session.hpp"
#include "mobile3d/editor/transform_gizmo.hpp"

#include <array>
#include <vector>

namespace m3d {

// Platform-independent transform driver used by viewport gizmo input. It owns
// no rendering state: Vulkan/Qt only provide gizmo-space drag components.
class TransformManipulator final {
public:
    TransformManipulator() = default;
    ~TransformManipulator() = default;

    TransformManipulator(const TransformManipulator&) = delete;
    TransformManipulator& operator=(const TransformManipulator&) = delete;

    [[nodiscard]] bool beginTranslate(EditorSession& session,
                                      TransformSpace space,
                                      TransformConstraint constraint,
                                      TransformSnapSettings snapping = {});
    [[nodiscard]] bool updateTranslation(Vec3 gizmoComponents);
    [[nodiscard]] bool beginRotate(EditorSession& session,
                                   TransformSpace space,
                                   TransformConstraint axisConstraint,
                                   PivotMode pivotMode,
                                   TransformSnapSettings snapping = {});
    [[nodiscard]] bool updateRotation(float angleRadians);
    [[nodiscard]] bool commit();
    [[nodiscard]] bool cancel();

    [[nodiscard]] bool active() const noexcept { return session_ != nullptr; }
    [[nodiscard]] TransformTool tool() const noexcept { return tool_; }
    [[nodiscard]] TransformSpace space() const noexcept { return space_; }
    [[nodiscard]] TransformConstraint constraint() const noexcept { return constraint_; }
    [[nodiscard]] PivotMode pivotMode() const noexcept { return pivotMode_; }
    [[nodiscard]] Vec3 pivotWorld() const noexcept { return pivotWorld_; }
    [[nodiscard]] const GizmoBasis& basis() const noexcept { return basis_; }

private:
    struct Target final {
        ObjectId object{};
        Transform initialLocal{};
        std::array<float, 9> inverseParentWorldLinear{};
        Vec3 parentWorldPosition{};
        Vec3 initialWorldPosition{};
        Quat parentWorldRotation{};
        Quat initialWorldRotation{};
    };

    void reset() noexcept;

    EditorSession* session_{nullptr};
    TransformTool tool_{TransformTool::Translate};
    TransformSpace space_{TransformSpace::Global};
    TransformConstraint constraint_{TransformConstraint::Free};
    PivotMode pivotMode_{PivotMode::Median};
    Vec3 pivotWorld_{};
    TransformSnapSettings snapping_{};
    GizmoBasis basis_{};
    std::vector<Target> targets_;
};

} // namespace m3d
