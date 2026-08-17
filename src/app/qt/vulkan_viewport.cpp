#include "vulkan_viewport.hpp"

#include "editor_controller.hpp"
#include "vulkan_viewport_renderer.hpp"

#include "mobile3d/editor/mesh_screen_picker.hpp"

#include <QEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QRect>
#include <QRectF>
#include <QSGRendererInterface>
#include <QTouchEvent>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr float kOrbitRadiansPerPixel = 0.006F;
constexpr float kWheelZoomExponentPerUnit = 0.0015F;
constexpr float kDegreesToRadians = 0.01745329251994329577F;
constexpr float kGizmoPixelSize = 90.0F;
constexpr qreal kTapSlop = 7.0;
constexpr qreal kGizmoHitTolerance = 13.0;
constexpr qreal kGizmoCenterHitRadius = 15.0;

struct GizmoHit final {
    m3d::TransformConstraint constraint{m3d::TransformConstraint::Free};
    QPointF screenDirection{};
    float screenLength{1.0F};
    QPointF planeVectorA{};
    QPointF planeVectorB{};
};

QString graphicsApiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::Software: return QStringLiteral("Software");
    case QSGRendererInterface::OpenVG: return QStringLiteral("OpenVG");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D 11");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("Direct3D 12");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    case QSGRendererInterface::Null: return QStringLiteral("Null");
    case QSGRendererInterface::Unknown: break;
    }
    return QStringLiteral("Unknown");
}

[[nodiscard]] QPointF touchCentroid(const QList<QEventPoint>& points) {
    if (points.isEmpty()) return {};
    QPointF sum;
    for (const auto& point : points) sum += point.position();
    return sum / static_cast<qreal>(points.size());
}

[[nodiscard]] float touchSpan(const QList<QEventPoint>& points) {
    if (points.size() < 2) return 0.0F;
    const QPointF delta = points.at(1).position() - points.at(0).position();
    return std::hypot(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
}

[[nodiscard]] bool exceedsTapSlop(QPointF delta) noexcept {
    return std::hypot(delta.x(), delta.y()) > kTapSlop;
}

[[nodiscard]] m3d::Vec3 addVector(m3d::Vec3 left, m3d::Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] m3d::Vec3 subtractVector(m3d::Vec3 left, m3d::Vec3 right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] m3d::Vec3 scaleVector(m3d::Vec3 value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] float dotVector(m3d::Vec3 left, m3d::Vec3 right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] float dotPoint(QPointF left, QPointF right) noexcept {
    return static_cast<float>(left.x() * right.x() + left.y() * right.y());
}

[[nodiscard]] float pointLength(QPointF value) noexcept {
    return std::hypot(static_cast<float>(value.x()), static_cast<float>(value.y()));
}

[[nodiscard]] m3d::Vec3 objectWorldPosition(const m3d::RenderObjectSnapshot& object) noexcept {
    return {
        object.worldTransform.at(0, 3),
        object.worldTransform.at(1, 3),
        object.worldTransform.at(2, 3),
    };
}

[[nodiscard]] bool hasSelectedAncestor(const m3d::RenderSceneSnapshot& snapshot,
                                       const m3d::RenderObjectSnapshot& object) noexcept {
    auto parent = object.parent;
    while (parent) {
        const auto* parentObject = snapshot.find(*parent);
        if (!parentObject) break;
        if (parentObject->selected) return true;
        parent = parentObject->parent;
    }
    return false;
}

[[nodiscard]] const m3d::RenderObjectSnapshot* activeSelectedObject(
    const m3d::RenderSceneSnapshot& snapshot) noexcept {
    const m3d::RenderObjectSnapshot* firstSelected = nullptr;
    for (const auto& object : snapshot.objects()) {
        if (!object.selected) continue;
        if (!firstSelected) firstSelected = &object;
        if (object.active) return &object;
    }
    return firstSelected;
}

[[nodiscard]] VulkanGizmoPresentation makeGizmoPresentation(
    const m3d::RenderSceneSnapshot& snapshot,
    const EditorController* controller,
    std::optional<m3d::TransformConstraint> activeConstraint = std::nullopt) {
    VulkanGizmoPresentation presentation;
    if (!controller || controller->editMode()) return presentation;

    const auto* active = activeSelectedObject(snapshot);
    if (!active) return presentation;

    std::vector<const m3d::RenderObjectSnapshot*> roots;
    for (const auto& object : snapshot.objects()) {
        if (object.selected && !hasSelectedAncestor(snapshot, object)) roots.push_back(&object);
    }
    if (roots.empty() || std::any_of(roots.cbegin(), roots.cend(),
                                     [](const auto* object) { return object->locked; })) {
        return presentation;
    }

    presentation.visible = true;
    presentation.tool = controller->transformToolValue();
    presentation.space = controller->transformSpaceValue();
    presentation.pivotMode = controller->pivotModeValue();
    presentation.basis = m3d::makeGizmoBasis(presentation.space, active->worldRotation);
    presentation.activeConstraint = activeConstraint;

    if (presentation.pivotMode == m3d::PivotMode::Active ||
        presentation.pivotMode == m3d::PivotMode::IndividualOrigins) {
        presentation.pivotWorld = objectWorldPosition(*active);
    } else {
        m3d::Vec3 sum{};
        for (const auto* object : roots) sum = addVector(sum, objectWorldPosition(*object));
        presentation.pivotWorld = scaleVector(sum, 1.0F / static_cast<float>(roots.size()));
    }
    return presentation;
}

struct ProjectedPoint final { QPointF screen{}; float depth{1.0F}; };

[[nodiscard]] std::optional<ProjectedPoint> projectWorldWithDepth(
    const m3d::Mat4& viewProjection, m3d::Vec3 world, qreal width, qreal height) noexcept {
    const float clipX=viewProjection.at(0,0)*world.x+viewProjection.at(0,1)*world.y+viewProjection.at(0,2)*world.z+viewProjection.at(0,3);
    const float clipY=viewProjection.at(1,0)*world.x+viewProjection.at(1,1)*world.y+viewProjection.at(1,2)*world.z+viewProjection.at(1,3);
    const float clipZ=viewProjection.at(2,0)*world.x+viewProjection.at(2,1)*world.y+viewProjection.at(2,2)*world.z+viewProjection.at(2,3);
    const float clipW=viewProjection.at(3,0)*world.x+viewProjection.at(3,1)*world.y+viewProjection.at(3,2)*world.z+viewProjection.at(3,3);
    if (clipW<=1.0e-5F) return std::nullopt;
    const float ndcX=clipX/clipW, ndcY=clipY/clipW, ndcZ=clipZ/clipW;
    if(!std::isfinite(ndcX)||!std::isfinite(ndcY)||!std::isfinite(ndcZ)) return std::nullopt;
    return ProjectedPoint{QPointF((static_cast<qreal>(ndcX)*0.5+0.5)*width,(static_cast<qreal>(ndcY)*0.5+0.5)*height),ndcZ};
}

[[nodiscard]] std::optional<QPointF> projectWorld(const m3d::Mat4& viewProjection,m3d::Vec3 world,qreal width,qreal height) noexcept {
    const auto projected=projectWorldWithDepth(viewProjection,world,width,height);
    return projected?std::optional<QPointF>(projected->screen):std::nullopt;
}

[[nodiscard]] m3d::Vec3 transformPoint(const m3d::Mat4& matrix,m3d::Vec3 point) noexcept {
    return {matrix.at(0,0)*point.x+matrix.at(0,1)*point.y+matrix.at(0,2)*point.z+matrix.at(0,3),
            matrix.at(1,0)*point.x+matrix.at(1,1)*point.y+matrix.at(1,2)*point.z+matrix.at(1,3),
            matrix.at(2,0)*point.x+matrix.at(2,1)*point.y+matrix.at(2,2)*point.z+matrix.at(2,3)};
}

[[nodiscard]] float cross2D(QPointF a, QPointF b, QPointF c) noexcept {
    return static_cast<float>((b.x() - a.x()) * (c.y() - a.y()) -
                              (b.y() - a.y()) * (c.x() - a.x()));
}

[[nodiscard]] bool pointInTriangle(QPointF point, QPointF a, QPointF b, QPointF c) noexcept {
    const float c1 = cross2D(a, b, point);
    const float c2 = cross2D(b, c, point);
    const float c3 = cross2D(c, a, point);
    const bool hasNegative = c1 < 0.0F || c2 < 0.0F || c3 < 0.0F;
    const bool hasPositive = c1 > 0.0F || c2 > 0.0F || c3 > 0.0F;
    return !(hasNegative && hasPositive);
}

[[nodiscard]] bool pointInQuad(QPointF point, QPointF a, QPointF b,
                               QPointF c, QPointF d) noexcept {
    return pointInTriangle(point, a, b, c) || pointInTriangle(point, a, c, d);
}

[[nodiscard]] bool isPlaneConstraint(m3d::TransformConstraint constraint) noexcept {
    return constraint == m3d::TransformConstraint::XY ||
           constraint == m3d::TransformConstraint::XZ ||
           constraint == m3d::TransformConstraint::YZ;
}

[[nodiscard]] float distanceToSegment(QPointF point, QPointF start, QPointF end,
                                      QPointF* direction = nullptr,
                                      float* segmentLength = nullptr) noexcept {
    const QPointF segment = end - start;
    const float lengthSquared = dotPoint(segment, segment);
    if (lengthSquared <= 1.0e-6F) return std::numeric_limits<float>::infinity();
    const float length = std::sqrt(lengthSquared);
    const float t = std::clamp(dotPoint(point - start, segment) / lengthSquared, 0.0F, 1.0F);
    const QPointF closest = start + segment * static_cast<qreal>(t);
    if (direction) *direction = segment / static_cast<qreal>(length);
    if (segmentLength) *segmentLength = length;
    return pointLength(point - closest);
}

[[nodiscard]] std::optional<GizmoHit> hitAxisGizmo(
    QPointF point,
    const VulkanGizmoPresentation& gizmo,
    const m3d::Mat4& viewProjection,
    qreal width,
    qreal height) {
    const auto pivotScreen = projectWorld(viewProjection, gizmo.pivotWorld, width, height);
    if (!pivotScreen) return std::nullopt;

    if ((gizmo.tool == m3d::TransformTool::Translate ||
         gizmo.tool == m3d::TransformTool::Scale) &&
        pointLength(point - *pivotScreen) <= static_cast<float>(kGizmoCenterHitRadius)) {
        return GizmoHit{m3d::TransformConstraint::Free, {}, 1.0F};
    }

    if (gizmo.tool == m3d::TransformTool::Scale &&
        (gizmo.space != m3d::TransformSpace::Local ||
         gizmo.pivotMode != m3d::PivotMode::IndividualOrigins)) {
        return std::nullopt;
    }

    struct PlaneCandidate final {
        m3d::TransformConstraint constraint;
        m3d::Vec3 axisA;
        m3d::Vec3 axisB;
    };
    const std::array<PlaneCandidate, 3> planes{{
        {m3d::TransformConstraint::XY, gizmo.basis.x, gizmo.basis.y},
        {m3d::TransformConstraint::XZ, gizmo.basis.x, gizmo.basis.z},
        {m3d::TransformConstraint::YZ, gizmo.basis.y, gizmo.basis.z},
    }};
    constexpr float planeInner = 0.22F;
    constexpr float planeOuter = 0.42F;
    for (const auto& plane : planes) {
        const auto worldCorner = [&](float a, float b) {
            return addVector(gizmo.pivotWorld,
                             addVector(scaleVector(plane.axisA, gizmo.worldSize * a),
                                       scaleVector(plane.axisB, gizmo.worldSize * b)));
        };
        const auto p00 = projectWorld(viewProjection, worldCorner(planeInner, planeInner), width, height);
        const auto p10 = projectWorld(viewProjection, worldCorner(planeOuter, planeInner), width, height);
        const auto p11 = projectWorld(viewProjection, worldCorner(planeOuter, planeOuter), width, height);
        const auto p01 = projectWorld(viewProjection, worldCorner(planeInner, planeOuter), width, height);
        const auto axisAEnd = projectWorld(
            viewProjection, addVector(gizmo.pivotWorld, scaleVector(plane.axisA, gizmo.worldSize)),
            width, height);
        const auto axisBEnd = projectWorld(
            viewProjection, addVector(gizmo.pivotWorld, scaleVector(plane.axisB, gizmo.worldSize)),
            width, height);
        if (!p00 || !p10 || !p11 || !p01 || !axisAEnd || !axisBEnd) continue;
        if (pointInQuad(point, *p00, *p10, *p11, *p01)) {
            return GizmoHit{plane.constraint, {}, 1.0F,
                            *axisAEnd - *pivotScreen, *axisBEnd - *pivotScreen};
        }
    }

    struct Candidate final {
        m3d::TransformConstraint constraint;
        m3d::Vec3 axis;
    };
    const std::array<Candidate, 3> candidates{{
        {m3d::TransformConstraint::X, gizmo.basis.x},
        {m3d::TransformConstraint::Y, gizmo.basis.y},
        {m3d::TransformConstraint::Z, gizmo.basis.z},
    }};

    float bestDistance = std::numeric_limits<float>::infinity();
    std::optional<GizmoHit> best;
    for (const auto& candidate : candidates) {
        const m3d::Vec3 axisStartWorld = addVector(
            gizmo.pivotWorld, scaleVector(candidate.axis, gizmo.worldSize * 0.17F));
        const m3d::Vec3 axisEndWorld = addVector(
            gizmo.pivotWorld, scaleVector(candidate.axis, gizmo.worldSize * 1.08F));
        const auto start = projectWorld(viewProjection, axisStartWorld, width, height);
        const auto end = projectWorld(viewProjection, axisEndWorld, width, height);
        if (!start || !end) continue;
        QPointF direction;
        float length = 0.0F;
        const float distance = distanceToSegment(point, *start, *end, &direction, &length);
        if (distance < bestDistance && distance <= static_cast<float>(kGizmoHitTolerance)) {
            bestDistance = distance;
            best = GizmoHit{candidate.constraint, direction, length / 0.91F};
        }
    }
    return best;
}

[[nodiscard]] std::optional<GizmoHit> hitRotationGizmo(
    QPointF point,
    const VulkanGizmoPresentation& gizmo,
    const m3d::Mat4& viewProjection,
    qreal width,
    qreal height) {
    struct Ring final {
        m3d::TransformConstraint constraint;
        m3d::Vec3 tangentA;
        m3d::Vec3 tangentB;
    };
    const std::array<Ring, 3> rings{{
        {m3d::TransformConstraint::X, gizmo.basis.y, gizmo.basis.z},
        {m3d::TransformConstraint::Y, gizmo.basis.x, gizmo.basis.z},
        {m3d::TransformConstraint::Z, gizmo.basis.x, gizmo.basis.y},
    }};
    const auto pivotScreen = projectWorld(viewProjection, gizmo.pivotWorld, width, height);
    if (!pivotScreen) return std::nullopt;

    constexpr int segments = 64;
    constexpr float twoPi = 6.28318530717958647692F;
    float bestDistance = std::numeric_limits<float>::infinity();
    std::optional<GizmoHit> best;
    for (const auto& ring : rings) {
        std::optional<QPointF> previous;
        for (int index = 0; index <= segments; ++index) {
            const float angle = twoPi * static_cast<float>(index) / static_cast<float>(segments);
            const m3d::Vec3 radial = addVector(scaleVector(ring.tangentA, std::cos(angle)),
                                                scaleVector(ring.tangentB, std::sin(angle)));
            const auto current = projectWorld(
                viewProjection,
                addVector(gizmo.pivotWorld, scaleVector(radial, gizmo.worldSize)),
                width, height);
            if (previous && current) {
                QPointF tangent;
                const float distance = distanceToSegment(point, *previous, *current, &tangent, nullptr);
                if (distance < bestDistance && distance <= static_cast<float>(kGizmoHitTolerance)) {
                    const float radius = std::max(pointLength((*previous + *current) * 0.5 - *pivotScreen), 8.0F);
                    bestDistance = distance;
                    best = GizmoHit{ring.constraint, tangent, radius};
                }
            }
            previous = current;
        }
    }
    return best;
}

[[nodiscard]] std::optional<GizmoHit> hitGizmo(
    QPointF point,
    const VulkanGizmoPresentation& gizmo,
    const m3d::Mat4& viewProjection,
    qreal width,
    qreal height) {
    if (!gizmo.visible) return std::nullopt;
    return gizmo.tool == m3d::TransformTool::Rotate
        ? hitRotationGizmo(point, gizmo, viewProjection, width, height)
        : hitAxisGizmo(point, gizmo, viewProjection, width, height);
}

} // namespace

VulkanViewport::VulkanViewport(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, false);
    setAcceptTouchEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setFocus(true);
    connect(this, &QQuickItem::windowChanged, this, &VulkanViewport::handleWindowChanged);
}

VulkanViewport::~VulkanViewport() {
    if (connectedWindow_) disconnect(connectedWindow_, nullptr, this, nullptr);
    delete renderer_;
}

QObject* VulkanViewport::controller() const noexcept { return controller_.data(); }

void VulkanViewport::setController(QObject* controller) {
    auto* typedController = qobject_cast<EditorController*>(controller);
    if (controller_.data() == typedController) return;
    if (controllerProjectConnection_) disconnect(controllerProjectConnection_);
    if (controllerSelectionConnection_) disconnect(controllerSelectionConnection_);
    if (controllerTransformConnection_) disconnect(controllerTransformConnection_);
    if (controllerTransformActivityConnection_) disconnect(controllerTransformActivityConnection_);
    controller_ = typedController;
    if (controller_) {
        controllerProjectConnection_ = connect(controller_, &EditorController::projectStateChanged,
                                               this, &QQuickItem::update);
        controllerSelectionConnection_ = connect(controller_, &EditorController::selectionChanged,
                                                 this, &QQuickItem::update);
        controllerTransformConnection_ = connect(controller_, &EditorController::transformSettingsChanged,
                                                 this, &QQuickItem::update);
        controllerTransformActivityConnection_ = connect(controller_, &EditorController::transformActivityChanged,
                                                         this, &QQuickItem::update);
    }
    emit controllerChanged();
    update();
}

QString VulkanViewport::projectionName() const {
    return camera_.projection() == m3d::CameraProjection::Perspective
        ? QStringLiteral("Perspective") : QStringLiteral("Orthographic");
}

void VulkanViewport::toggleProjection() {
    if (transformInteraction_) return;
    camera_.setProjection(camera_.projection() == m3d::CameraProjection::Perspective
                              ? m3d::CameraProjection::Orthographic
                              : m3d::CameraProjection::Perspective);
    cameraMutated();
}

void VulkanViewport::resetCamera() {
    if (transformInteraction_) return;
    camera_ = m3d::ViewportCamera{};
    cameraMutated();
}

void VulkanViewport::requestPickAt(double x,double y) { requestPickAtInternal(QPointF(x,y),false); }

void VulkanViewport::requestPickAtInternal(QPointF position,bool toggle) {
    if(transformInteraction_||position.x()<0.0||position.y()<0.0||position.x()>=width()||position.y()>=height()) return;
    if(controller_&&controller_->editMode()){ (void)pickMeshElementAt(position,toggle); update(); return; }
    pendingPickPosition_=position; pickRequested_=true; update();
}

bool VulkanViewport::pickMeshElementAt(QPointF position,bool toggle) {
    if(!controller_||!controller_->editMode()||width()<=0.0||height()<=0.0) return false;
    const auto editMesh=controller_->meshEditSnapshot(); const auto scene=controller_->renderSnapshot();
    const auto* editObject=editMesh.active()?scene.find(editMesh.object):nullptr;
    if(!editObject){ (void)controller_->clearMeshSelection(); return true; }
    const float aspect=static_cast<float>(width()/std::max(height(),1.0)); const auto viewProjection=camera_.viewProjectionMatrix(aspect);
    std::vector<m3d::MeshScreenVertex> vertices; std::vector<m3d::MeshScreenEdge> edges; std::vector<m3d::MeshScreenFace> faces;
    std::unordered_map<std::uint32_t,m3d::MeshScreenPoint> projectedById; vertices.reserve(editMesh.vertices.size()); projectedById.reserve(editMesh.vertices.size());
    for(const auto& vertex:editMesh.vertices){
        const auto projected=projectWorldWithDepth(viewProjection,transformPoint(editObject->worldTransform,vertex.position),width(),height());
        if(!projected||projected->depth<0.0F||projected->depth>1.0F) continue;
        const m3d::MeshScreenPoint point{static_cast<float>(projected->screen.x()),static_cast<float>(projected->screen.y()),projected->depth};
        projectedById.emplace(vertex.id.value,point); vertices.push_back({vertex.id,point});
    }
    edges.reserve(editMesh.edges.size());
    for(const auto& edge:editMesh.edges){ const auto a=projectedById.find(edge.first.value),b=projectedById.find(edge.second.value); if(a!=projectedById.end()&&b!=projectedById.end()) edges.push_back({edge.id,a->second,b->second}); }
    faces.reserve(editMesh.faces.size());
    for(const auto& face:editMesh.faces){ m3d::MeshScreenFace projectedFace; projectedFace.id=face.id; projectedFace.vertices.reserve(face.vertices.size()); bool complete=true;
        for(const auto vertexId:face.vertices){ const auto found=projectedById.find(vertexId.value); if(found==projectedById.end()){ complete=false; break; } projectedFace.vertices.push_back(found->second); }
        if(complete&&projectedFace.vertices.size()>=3U) faces.push_back(std::move(projectedFace));
    }
    m3d::MeshScreenPickRequest request; request.mode=editMesh.mode; request.x=static_cast<float>(position.x()); request.y=static_cast<float>(position.y());
    const auto hit=m3d::MeshScreenPicker::pick(vertices,edges,faces,request); if(!hit){ (void)controller_->clearMeshSelection(); return true; }
    QString type; switch(hit->mode){ case m3d::MeshSelectionMode::Vertex:type=QStringLiteral("Vertex");break; case m3d::MeshSelectionMode::Edge:type=QStringLiteral("Edge");break; case m3d::MeshSelectionMode::Face:type=QStringLiteral("Face");break; }
    (void)controller_->selectMeshElement(type,static_cast<int>(hit->elementId),toggle); return true;
}

void VulkanViewport::releaseResources() {
    // Device-owned state is released from sceneGraphInvalidated on the render thread.
}

void VulkanViewport::keyPressEvent(QKeyEvent* event) {
    if (transformInteraction_ && event->key() == Qt::Key_Escape) {
        finishGizmoTransform(false);
        event->accept();
        return;
    }
    QQuickItem::keyPressEvent(event);
}

void VulkanViewport::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && tryBeginGizmoTransform(event->position())) {
        event->accept();
        return;
    }
    navigationButton_ = event->button();
    lastMousePosition_ = event->position();
    mousePressPosition_ = event->position();
    mouseDragExceeded_ = false;
    event->accept();
}

void VulkanViewport::mouseMoveEvent(QMouseEvent* event) {
    if (transformInteraction_) {
        (void)updateGizmoTransform(event->position());
        event->accept();
        return;
    }
    if (navigationButton_ == Qt::NoButton) {
        event->ignore();
        return;
    }
    const QPointF delta = event->position() - lastMousePosition_;
    lastMousePosition_ = event->position();
    if (!mouseDragExceeded_ && exceedsTapSlop(event->position() - mousePressPosition_)) {
        mouseDragExceeded_ = true;
    }
    if (navigationButton_ == Qt::LeftButton) orbitByPixels(delta); else panByPixels(delta);
    event->accept();
}

void VulkanViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (transformInteraction_) {
        finishGizmoTransform(true);
        event->accept();
        return;
    }
    const bool shouldPick = navigationButton_ == Qt::LeftButton && !mouseDragExceeded_;
    navigationButton_ = Qt::NoButton;
    if (shouldPick) {
        const bool toggle=(event->modifiers() & (Qt::ShiftModifier|Qt::ControlModifier))!=0;
        requestPickAtInternal(event->position(),toggle);
    }
    event->accept();
}

void VulkanViewport::wheelEvent(QWheelEvent* event) {
    if (transformInteraction_) {
        event->accept();
        return;
    }
    const int wheelUnits = event->angleDelta().y();
    if (wheelUnits == 0) {
        event->ignore();
        return;
    }
    camera_.zoom(std::exp(static_cast<float>(wheelUnits) * kWheelZoomExponentPerUnit));
    cameraMutated();
    event->accept();
}

void VulkanViewport::touchEvent(QTouchEvent* event) {
    const auto points = event->points();
    const qsizetype pointCount = points.size();

    if (transformInteraction_) {
        if (event->type() == QEvent::TouchCancel) {
            finishGizmoTransform(false);
        } else if (event->type() == QEvent::TouchEnd || pointCount == 0) {
            finishGizmoTransform(true);
        } else if (pointCount != 1) {
            finishGizmoTransform(false);
        } else {
            (void)updateGizmoTransform(points.constFirst().position());
        }
        event->accept();
        return;
    }

    if (event->type() == QEvent::TouchBegin && pointCount == 1 &&
        tryBeginGizmoTransform(points.constFirst().position())) {
        lastTouchPointCount_ = 1;
        lastTouchCentroid_ = points.constFirst().position();
        touchStartCentroid_ = lastTouchCentroid_;
        touchDragExceeded_ = true;
        event->accept();
        return;
    }

    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel || pointCount == 0) {
        if (event->type() == QEvent::TouchEnd && lastTouchPointCount_ == 1 && !touchDragExceeded_) {
            requestPickAt(lastTouchCentroid_.x(), lastTouchCentroid_.y());
        }
        lastTouchPointCount_ = 0;
        lastTouchSpan_ = 0.0F;
        lastTouchCentroid_ = {};
        touchStartCentroid_ = {};
        touchDragExceeded_ = false;
        event->accept();
        return;
    }

    const QPointF centroid = touchCentroid(points);
    const float span = touchSpan(points);
    if (event->type() == QEvent::TouchBegin || pointCount != lastTouchPointCount_) {
        lastTouchPointCount_ = pointCount;
        lastTouchCentroid_ = centroid;
        touchStartCentroid_ = centroid;
        lastTouchSpan_ = span;
        touchDragExceeded_ = pointCount > 1;
        event->accept();
        return;
    }

    if (pointCount == 1) {
        if (!touchDragExceeded_ && exceedsTapSlop(centroid - touchStartCentroid_)) {
            touchDragExceeded_ = true;
        }
        orbitByPixels(centroid - lastTouchCentroid_);
    } else {
        touchDragExceeded_ = true;
        panByPixels(centroid - lastTouchCentroid_);
        if (lastTouchSpan_ > 1.0F && span > 1.0F) {
            camera_.zoom(span / lastTouchSpan_);
            cameraMutated();
        }
    }

    lastTouchCentroid_ = centroid;
    lastTouchSpan_ = span;
    lastTouchPointCount_ = pointCount;
    event->accept();
}

void VulkanViewport::handleWindowChanged(QQuickWindow* window) {
    if (connectedWindow_) disconnect(connectedWindow_, nullptr, this, nullptr);
    connectedWindow_ = window;
    if (!window) {
        if (transformInteraction_) finishGizmoTransform(false);
        if (vulkanActive_ || backendName_ != QStringLiteral("Unavailable")) {
            vulkanActive_ = false;
            backendName_ = QStringLiteral("Unavailable");
            emit backendChanged();
        }
        return;
    }
    connect(window, &QQuickWindow::beforeSynchronizing,
            this, &VulkanViewport::sync, Qt::DirectConnection);
    connect(window, &QQuickWindow::beforeRendering,
            this, &VulkanViewport::recordPickCommands, Qt::DirectConnection);
    connect(window, &QQuickWindow::beforeRenderPassRecording,
            this, &VulkanViewport::recordVulkanCommands, Qt::DirectConnection);
    connect(window, &QQuickWindow::sceneGraphInvalidated,
            this, &VulkanViewport::cleanup, Qt::DirectConnection);
    updateBackendState(window);
    update();
}

void VulkanViewport::sync() {
    auto* currentWindow = window();
    if (!currentWindow) return;
    if (!renderer_) renderer_ = new VulkanViewportRenderer;

    const QRectF sceneRect = mapRectToScene(boundingRect());
    const qreal dpr = currentWindow->devicePixelRatio();
    const int left = static_cast<int>(std::floor(static_cast<double>(sceneRect.left()) * dpr));
    const int top = static_cast<int>(std::floor(static_cast<double>(sceneRect.top()) * dpr));
    const int right = static_cast<int>(std::ceil(static_cast<double>(sceneRect.right()) * dpr));
    const int bottom = static_cast<int>(std::ceil(static_cast<double>(sceneRect.bottom()) * dpr));
    const int pixelWidth = std::max(0, right - left);
    const int pixelHeight = std::max(0, bottom - top);

    m3d::RenderSceneSnapshot snapshot;
    m3d::MeshEditPresentationSnapshot editMesh;
    if (controller_) {
        snapshot = controller_->renderSnapshot();
        editMesh = controller_->meshEditSnapshot();
    }
    const float aspect = pixelHeight > 0
        ? static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight) : 1.0F;
    const m3d::Mat4 viewProjection = camera_.viewProjectionMatrix(aspect);
    auto gizmo = makeGizmoPresentation(
        snapshot, controller_.data(),
        transformInteraction_ ? std::optional<m3d::TransformConstraint>(transformDragConstraint_)
                              : std::nullopt);
    if (gizmo.visible) gizmo.worldSize = worldUnitsPerPixelAt(gizmo.pivotWorld) * kGizmoPixelSize;
    renderer_->synchronize(QRect(left, top, pixelWidth, pixelHeight), std::move(snapshot),
                           std::move(editMesh), viewProjection, gizmo);

    if (pickRequested_ && pixelWidth > 0 && pixelHeight > 0) {
        const int x = std::clamp(
            static_cast<int>(std::floor(static_cast<double>(pendingPickPosition_.x()) * dpr)),
            0, pixelWidth - 1);
        const int y = std::clamp(
            static_cast<int>(std::floor(static_cast<double>(pendingPickPosition_.y()) * dpr)),
            0, pixelHeight - 1);
        renderer_->requestPick(QPoint(x, y));
        pickRequested_ = false;
    }
}

void VulkanViewport::cleanup() {
    if (renderer_) renderer_->release();
    delete renderer_;
    renderer_ = nullptr;
}

void VulkanViewport::recordPickCommands() {
    if (!renderer_) return;
    const auto result = renderer_->recordPicking(window());
    if (result.resultReady) {
        completedPickCount_.fetch_add(1, std::memory_order_relaxed);
        if (result.object) successfulPickCount_.fetch_add(1, std::memory_order_relaxed);
        const auto pickedObject = result.object;
        QMetaObject::invokeMethod(this, [this, pickedObject] {
            if (!controller_ || transformInteraction_) return;
            if (pickedObject) {
                (void)controller_->selectObject(QString::fromStdString(pickedObject->toString()), false);
            } else {
                controller_->clearSelection();
            }
        }, Qt::QueuedConnection);
    }
    if (result.waitingForReadback) scheduleNextFrame();
}

void VulkanViewport::recordVulkanCommands() {
    if (!renderer_) return;
    const auto stats = renderer_->record(window());
    if (stats.recorded) recordedFrameCount_.fetch_add(1, std::memory_order_relaxed);
    if (stats.meshDraws > 0) {
        recordedMeshDrawCount_.fetch_add(stats.meshDraws, std::memory_order_relaxed);
    }
    if (stats.outlineDraws > 0) {
        recordedOutlineDrawCount_.fetch_add(stats.outlineDraws, std::memory_order_relaxed);
    }
    if (stats.gizmoDraws > 0) {
        recordedGizmoDrawCount_.fetch_add(stats.gizmoDraws, std::memory_order_relaxed);
    }
    if (stats.editOverlayDraws > 0) {
        recordedEditOverlayDrawCount_.fetch_add(stats.editOverlayDraws, std::memory_order_relaxed);
    }
    if (stats.pipelineCacheLoaded) {
        pipelineCacheLoaded_.store(true, std::memory_order_relaxed);
    }
    if (stats.needsAnotherFrame) scheduleNextFrame();
}

void VulkanViewport::updateBackendState(QQuickWindow* window) {
    const auto api = window && window->rendererInterface()
        ? window->rendererInterface()->graphicsApi() : QSGRendererInterface::Unknown;
    const bool isVulkan = api == QSGRendererInterface::Vulkan;
    const QString name = graphicsApiName(api);
    if (vulkanActive_ == isVulkan && backendName_ == name) return;
    vulkanActive_ = isVulkan;
    backendName_ = name;
    emit backendChanged();
}

void VulkanViewport::orbitByPixels(QPointF delta) {
    if (transformInteraction_) return;
    camera_.orbit(-static_cast<float>(delta.x()) * kOrbitRadiansPerPixel,
                  -static_cast<float>(delta.y()) * kOrbitRadiansPerPixel);
    cameraMutated();
}

void VulkanViewport::panByPixels(QPointF delta) {
    if (transformInteraction_) return;
    const float units = worldUnitsPerPixel();
    camera_.pan(-static_cast<float>(delta.x()) * units,
                static_cast<float>(delta.y()) * units);
    cameraMutated();
}

float VulkanViewport::worldUnitsPerPixel() const noexcept {
    return worldUnitsPerPixelAt(camera_.target());
}

float VulkanViewport::worldUnitsPerPixelAt(m3d::Vec3 worldPoint) const noexcept {
    const float viewportHeight = std::max(static_cast<float>(height()), 1.0F);
    if (camera_.projection() == m3d::CameraProjection::Orthographic) {
        return camera_.orthographicHeight() / viewportHeight;
    }
    const m3d::Vec3 fromCamera = subtractVector(worldPoint, camera_.position());
    const float depth = std::max(dotVector(fromCamera, camera_.forward()), camera_.nearClip() * 2.0F);
    const float fovRadians = camera_.perspectiveFovDegrees() * kDegreesToRadians;
    return 2.0F * depth * std::tan(fovRadians * 0.5F) / viewportHeight;
}

bool VulkanViewport::tryBeginGizmoTransform(QPointF position) {
    if (!controller_ || transformInteraction_ || controller_->transformInProgress() ||
        width() <= 0.0 || height() <= 0.0) return false;
    const auto snapshot = controller_->renderSnapshot();
    auto gizmo = makeGizmoPresentation(snapshot, controller_.data());
    if (!gizmo.visible) return false;
    gizmo.worldSize = worldUnitsPerPixelAt(gizmo.pivotWorld) * kGizmoPixelSize;
    const float aspect = static_cast<float>(width() / std::max(height(), 1.0));
    const m3d::Mat4 viewProjection = camera_.viewProjectionMatrix(aspect);
    const auto hit = hitGizmo(position, gizmo, viewProjection, width(), height());
    if (!hit || !controller_->beginViewportTransform(hit->constraint)) return false;

    transformInteraction_ = true;
    transformDragConstraint_ = hit->constraint;
    transformDragBasis_ = gizmo.basis;
    transformDragPivotWorld_ = gizmo.pivotWorld;
    transformStartPosition_ = position;
    transformAxisScreenUnit_ = hit->screenDirection;
    transformAxisScreenLength_ = std::max(hit->screenLength, 1.0F);
    transformPlaneScreenVectorA_ = hit->planeVectorA;
    transformPlaneScreenVectorB_ = hit->planeVectorB;
    transformWorldSize_ = gizmo.worldSize;
    const auto pivotScreen = projectWorld(viewProjection, gizmo.pivotWorld, width(), height());
    transformPivotScreen_ = pivotScreen.value_or(position);
    transformStartAngle_ = std::atan2(
        static_cast<float>(position.y() - transformPivotScreen_.y()),
        static_cast<float>(position.x() - transformPivotScreen_.x()));
    navigationButton_ = Qt::NoButton;
    pickRequested_ = false;
    forceActiveFocus();
    update();
    return true;
}

bool VulkanViewport::updateGizmoTransform(QPointF position) {
    if (!controller_ || !transformInteraction_) return false;
    const QPointF screenDelta = position - transformStartPosition_;
    bool changed = false;

    if (controller_->transformToolValue() == m3d::TransformTool::Translate) {
        m3d::Vec3 components{};
        if (transformDragConstraint_ == m3d::TransformConstraint::Free) {
            const float units = worldUnitsPerPixelAt(transformDragPivotWorld_);
            const m3d::Vec3 worldDelta = addVector(
                scaleVector(camera_.right(), static_cast<float>(screenDelta.x()) * units),
                scaleVector(camera_.up(), -static_cast<float>(screenDelta.y()) * units));
            components = {
                dotVector(worldDelta, transformDragBasis_.x),
                dotVector(worldDelta, transformDragBasis_.y),
                dotVector(worldDelta, transformDragBasis_.z),
            };
        } else if (isPlaneConstraint(transformDragConstraint_)) {
            const float ax = static_cast<float>(transformPlaneScreenVectorA_.x());
            const float ay = static_cast<float>(transformPlaneScreenVectorA_.y());
            const float bx = static_cast<float>(transformPlaneScreenVectorB_.x());
            const float by = static_cast<float>(transformPlaneScreenVectorB_.y());
            const float dx = static_cast<float>(screenDelta.x());
            const float dy = static_cast<float>(screenDelta.y());
            const float determinant = ax * by - ay * bx;
            if (std::fabs(determinant) <= 1.0e-5F) return false;
            const float componentA = (dx * by - dy * bx) / determinant * transformWorldSize_;
            const float componentB = (ax * dy - ay * dx) / determinant * transformWorldSize_;
            if (transformDragConstraint_ == m3d::TransformConstraint::XY) {
                components.x = componentA; components.y = componentB;
            } else if (transformDragConstraint_ == m3d::TransformConstraint::XZ) {
                components.x = componentA; components.z = componentB;
            } else {
                components.y = componentA; components.z = componentB;
            }
        } else {
            const float pixels = dotPoint(screenDelta, transformAxisScreenUnit_);
            const float component = pixels / transformAxisScreenLength_ * transformWorldSize_;
            if (transformDragConstraint_ == m3d::TransformConstraint::X) components.x = component;
            if (transformDragConstraint_ == m3d::TransformConstraint::Y) components.y = component;
            if (transformDragConstraint_ == m3d::TransformConstraint::Z) components.z = component;
        }
        changed = controller_->updateViewportTranslation(components);
    } else if (controller_->transformToolValue() == m3d::TransformTool::Rotate) {
        const float pixels = dotPoint(screenDelta, transformAxisScreenUnit_);
        const float angle = pixels / transformAxisScreenLength_;
        changed = controller_->updateViewportRotation(angle);
    } else {
        float factor = 1.0F;
        if (transformDragConstraint_ == m3d::TransformConstraint::Free) {
            const float diagonal = static_cast<float>(screenDelta.x() - screenDelta.y());
            factor = std::exp(diagonal * 0.006F);
        } else if (isPlaneConstraint(transformDragConstraint_)) {
            const float ax = static_cast<float>(transformPlaneScreenVectorA_.x());
            const float ay = static_cast<float>(transformPlaneScreenVectorA_.y());
            const float bx = static_cast<float>(transformPlaneScreenVectorB_.x());
            const float by = static_cast<float>(transformPlaneScreenVectorB_.y());
            const float dx = static_cast<float>(screenDelta.x());
            const float dy = static_cast<float>(screenDelta.y());
            const float determinant = ax * by - ay * bx;
            if (std::fabs(determinant) <= 1.0e-5F) return false;
            const float a = (dx * by - dy * bx) / determinant;
            const float b = (ax * dy - ay * dx) / determinant;
            factor = std::max(0.01F, 1.0F + (a + b) * 0.5F);
        } else {
            const float pixels = dotPoint(screenDelta, transformAxisScreenUnit_);
            factor = std::max(0.01F, 1.0F + pixels / transformAxisScreenLength_);
        }
        changed = controller_->updateViewportScale(factor);
    }

    if (changed) update();
    return changed;
}

void VulkanViewport::finishGizmoTransform(bool commit) {
    if (!transformInteraction_) return;
    if (controller_) {
        if (commit) (void)controller_->commitViewportTransform();
        else (void)controller_->cancelViewportTransform();
    }
    transformInteraction_ = false;
    transformDragConstraint_ = m3d::TransformConstraint::Free;
    transformDragBasis_ = {};
    transformDragPivotWorld_ = {};
    transformStartPosition_ = {};
    transformPivotScreen_ = {};
    transformAxisScreenUnit_ = {};
    transformPlaneScreenVectorA_ = {};
    transformPlaneScreenVectorB_ = {};
    transformAxisScreenLength_ = 1.0F;
    transformWorldSize_ = 1.0F;
    transformStartAngle_ = 0.0F;
    navigationButton_ = Qt::NoButton;
    update();
}

void VulkanViewport::cameraMutated() {
    emit cameraChanged();
    update();
}

void VulkanViewport::scheduleNextFrame() {
    QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
}
