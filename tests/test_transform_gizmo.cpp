#include "test_harness.hpp"

#include "mobile3d/editor/transform_gizmo.hpp"

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846F;

bool near(float left, float right, float epsilon = 1.0e-4F) {
    return std::fabs(left - right) <= epsilon;
}

void requireVecNear(m3d::Vec3 value, m3d::Vec3 expected) {
    REQUIRE(near(value.x, expected.x));
    REQUIRE(near(value.y, expected.y));
    REQUIRE(near(value.z, expected.z));
}

} // namespace

TEST_CASE("global gizmo basis stays aligned to world axes") {
    const m3d::Quat arbitrary{0.2F, 0.3F, 0.4F, 0.8F};
    const auto basis = m3d::makeGizmoBasis(m3d::TransformSpace::Global, arbitrary);
    requireVecNear(basis.x, {1.0F, 0.0F, 0.0F});
    requireVecNear(basis.y, {0.0F, 1.0F, 0.0F});
    requireVecNear(basis.z, {0.0F, 0.0F, 1.0F});
}

TEST_CASE("local gizmo basis follows object world rotation") {
    const float halfAngle = kPi * 0.25F;
    const m3d::Quat rotateNinetyAroundZ{0.0F, 0.0F, std::sin(halfAngle), std::cos(halfAngle)};
    const auto basis = m3d::makeGizmoBasis(m3d::TransformSpace::Local, rotateNinetyAroundZ);
    requireVecNear(basis.x, {0.0F, 1.0F, 0.0F});
    requireVecNear(basis.y, {-1.0F, 0.0F, 0.0F});
    requireVecNear(basis.z, {0.0F, 0.0F, 1.0F});
}

TEST_CASE("translation constraints and snapping are applied in gizmo space") {
    m3d::TransformSnapSettings snapping;
    snapping.translationEnabled = true;
    snapping.translationStep = 0.5F;
    const auto basis = m3d::makeGizmoBasis(m3d::TransformSpace::Global, {});
    const auto delta = m3d::composeTranslationDelta(
        {1.26F, -0.74F, 8.0F}, m3d::TransformConstraint::XY, basis, snapping);
    requireVecNear(delta, {1.5F, -0.5F, 0.0F});
}

TEST_CASE("local axis translation maps through rotated basis") {
    const float halfAngle = kPi * 0.25F;
    const m3d::Quat rotateNinetyAroundZ{0.0F, 0.0F, std::sin(halfAngle), std::cos(halfAngle)};
    const auto basis = m3d::makeGizmoBasis(m3d::TransformSpace::Local, rotateNinetyAroundZ);
    const auto delta = m3d::composeTranslationDelta(
        {2.0F, 99.0F, 99.0F}, m3d::TransformConstraint::X, basis);
    requireVecNear(delta, {0.0F, 2.0F, 0.0F});
}

TEST_CASE("rotation snapping returns an axis-angle quaternion") {
    m3d::TransformSnapSettings snapping;
    snapping.rotationEnabled = true;
    snapping.rotationStepRadians = 15.0F * kPi / 180.0F;
    const auto basis = m3d::makeGizmoBasis(m3d::TransformSpace::Global, {});
    const auto rotation = m3d::composeRotationDelta(
        17.0F * kPi / 180.0F, m3d::TransformConstraint::Z, basis, snapping);
    const float expectedHalf = 7.5F * kPi / 180.0F;
    REQUIRE(near(rotation.x, 0.0F));
    REQUIRE(near(rotation.y, 0.0F));
    REQUIRE(near(rotation.z, std::sin(expectedHalf)));
    REQUIRE(near(rotation.w, std::cos(expectedHalf)));
}

TEST_CASE("rotation rejects plane constraints until trackball rotation exists") {
    const auto basis = m3d::makeGizmoBasis(m3d::TransformSpace::Global, {});
    const auto rotation = m3d::composeRotationDelta(
        0.5F, m3d::TransformConstraint::XY, basis);
    REQUIRE(rotation == m3d::Quat{});
}

TEST_CASE("scale constraints preserve untouched axes and snap around one") {
    m3d::TransformSnapSettings snapping;
    snapping.scaleEnabled = true;
    snapping.scaleStep = 0.1F;
    requireVecNear(m3d::composeScaleFactors(
                       1.26F, m3d::TransformConstraint::X, snapping),
                   {1.3F, 1.0F, 1.0F});
    requireVecNear(m3d::composeScaleFactors(
                       0.76F, m3d::TransformConstraint::YZ, snapping),
                   {1.0F, 0.8F, 0.8F});
    requireVecNear(m3d::composeScaleFactors(
                       2.0F, m3d::TransformConstraint::Free),
                   {2.0F, 2.0F, 2.0F});
}

TEST_CASE("invalid snap steps safely fall back to unsnapped values") {
    m3d::TransformSnapSettings snapping;
    snapping.translationEnabled = true;
    snapping.translationStep = 0.0F;
    const auto basis = m3d::makeGizmoBasis(m3d::TransformSpace::Global, {});
    const auto delta = m3d::composeTranslationDelta(
        {0.37F, 0.0F, 0.0F}, m3d::TransformConstraint::X, basis, snapping);
    requireVecNear(delta, {0.37F, 0.0F, 0.0F});
}
