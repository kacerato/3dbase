#pragma once

#include "mobile3d/core/math.hpp"
#include "mobile3d/render/viewport_camera.hpp"

namespace m3d {

[[nodiscard]] Mat4 transformMatrix(const Transform& transform) noexcept;

} // namespace m3d
