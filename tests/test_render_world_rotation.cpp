#include "test_harness.hpp"

#include "mobile3d/render/render_snapshot.hpp"

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846F;

bool near(float left, float right, float epsilon = 1.0e-4F) {
    return std::fabs(left - right) <= epsilon;
}

} // namespace

TEST_CASE("render snapshot world rotation composes hierarchy independently from parent scale") {
    m3d::Scene scene;
    m3d::SelectionModel selection;
    const auto parent = scene.createObject(m3d::ObjectType::Empty, "Parent");
    const auto child = scene.createObject(m3d::ObjectType::Mesh, "Child", parent);

    const float halfNinety = kPi * 0.25F;
    m3d::Transform parentTransform;
    parentTransform.rotation = {0.0F, 0.0F, std::sin(halfNinety), std::cos(halfNinety)};
    parentTransform.scale = {2.0F, 3.0F, 4.0F};
    REQUIRE(scene.setTransform(parent, parentTransform));

    const float halfFortyFive = kPi * 0.125F;
    m3d::Transform childTransform;
    childTransform.rotation = {std::sin(halfFortyFive), 0.0F, 0.0F, std::cos(halfFortyFive)};
    REQUIRE(scene.setTransform(child, childTransform));
    REQUIRE(selection.select(scene, child));

    const auto snapshot = m3d::RenderSnapshotBuilder::build(scene, selection, 1, 1);
    const auto* object = snapshot.find(child);
    REQUIRE(object != nullptr);

    // qParent(Z90) * qChild(X45).
    REQUIRE(near(object->worldRotation.x, 0.27059805F));
    REQUIRE(near(object->worldRotation.y, 0.27059805F));
    REQUIRE(near(object->worldRotation.z, 0.65328148F));
    REQUIRE(near(object->worldRotation.w, 0.65328148F));

    // The scaled world matrix is deliberately not used to infer this quaternion.
    REQUIRE(object->worldTransform.at(0, 0) != 1.0F);
    REQUIRE(object->active);
}
