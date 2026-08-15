#include "test_harness.hpp"

#include "mobile3d/render/viewport_camera.hpp"

#include <cmath>

namespace {

[[nodiscard]] bool near(float left, float right, float tolerance = 1.0e-4F) {
    return std::abs(left - right) <= tolerance;
}

} // namespace

TEST_CASE("viewport camera produces a stable right-handed view matrix") {
    m3d::ViewportCamera camera;
    camera.setTarget({0.0F, 0.0F, 0.0F});
    camera.setOrbit(0.0F, 0.0F);
    camera.setDistance(10.0F);

    const auto position = camera.position();
    REQUIRE(near(position.x, 0.0F));
    REQUIRE(near(position.y, 0.0F));
    REQUIRE(near(position.z, 10.0F));

    const auto view = camera.viewMatrix();
    REQUIRE(near(view.at(0, 0), 1.0F));
    REQUIRE(near(view.at(1, 1), 1.0F));
    REQUIRE(near(view.at(2, 2), 1.0F));
    REQUIRE(near(view.at(2, 3), -10.0F));
    REQUIRE(near(view.at(3, 3), 1.0F));
}

TEST_CASE("viewport camera uses Vulkan depth and inverted viewport Y projection") {
    m3d::ViewportCamera camera;
    REQUIRE(camera.setClipping(0.1F, 100.0F));
    camera.setPerspectiveFovDegrees(60.0F);

    const auto perspective = camera.projectionMatrix(2.0F);
    REQUIRE(perspective.at(0, 0) > 0.0F);
    REQUIRE(perspective.at(1, 1) < 0.0F);
    REQUIRE(perspective.at(2, 2) < 0.0F);
    REQUIRE(near(perspective.at(3, 2), -1.0F));

    camera.setProjection(m3d::CameraProjection::Orthographic);
    camera.setOrthographicHeight(20.0F);
    const auto orthographic = camera.projectionMatrix(2.0F);
    REQUIRE(near(orthographic.at(0, 0), 0.05F));
    REQUIRE(near(orthographic.at(1, 1), -0.1F));
    REQUIRE(near(orthographic.at(3, 3), 1.0F));
}

TEST_CASE("viewport orbit zoom and pan remain bounded and deterministic") {
    m3d::ViewportCamera camera;
    camera.setOrbit(0.0F, 0.0F);
    camera.setDistance(10.0F);

    camera.pan(2.0F, 3.0F);
    REQUIRE(near(camera.target().x, 2.0F));
    REQUIRE(near(camera.target().y, 3.0F));
    REQUIRE(near(camera.target().z, 0.0F));

    camera.zoom(2.0F);
    REQUIRE(near(camera.distance(), 5.0F));
    camera.zoom(0.5F);
    REQUIRE(near(camera.distance(), 10.0F));

    camera.orbit(0.0F, 100.0F);
    REQUIRE(near(camera.pitchRadians(), m3d::ViewportCamera::kMaxPitchRadians));
    camera.setDistance(0.0F);
    REQUIRE(near(camera.distance(), m3d::ViewportCamera::kMinDistance));

    camera.setProjection(m3d::CameraProjection::Orthographic);
    camera.setOrthographicHeight(10.0F);
    camera.zoom(2.0F);
    REQUIRE(near(camera.orthographicHeight(), 5.0F));
}

TEST_CASE("viewport camera rejects invalid clipping and composes view projection") {
    m3d::ViewportCamera camera;
    REQUIRE(!camera.setClipping(0.0F, 10.0F));
    REQUIRE(!camera.setClipping(10.0F, 1.0F));
    REQUIRE(camera.setClipping(0.25F, 500.0F));
    REQUIRE(near(camera.nearClip(), 0.25F));
    REQUIRE(near(camera.farClip(), 500.0F));

    const auto expected = m3d::multiply(camera.projectionMatrix(16.0F / 9.0F),
                                        camera.viewMatrix());
    const auto actual = camera.viewProjectionMatrix(16.0F / 9.0F);
    for (std::size_t index = 0; index < expected.values.size(); ++index) {
        REQUIRE(near(actual.values.at(index), expected.values.at(index)));
    }
}
