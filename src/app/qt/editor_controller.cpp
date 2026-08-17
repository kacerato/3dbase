#include "editor_controller.hpp"

#include "outliner_model.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <string>

namespace {

QString cleanProjectName(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) {
        return {};
    }
    return value.left(80);
}

QString slugFromName(QString value) {
    value = value.trimmed().toLower();
    for (qsizetype index = 0; index < value.size(); ++index) {
        if (!value.at(index).isLetterOrNumber()) {
            value[index] = QLatin1Char('_');
        }
    }
    while (value.contains(QStringLiteral("__"))) {
        value.replace(QStringLiteral("__"), QStringLiteral("_"));
    }
    value = value.trimmed();
    if (value.isEmpty()) {
        value = QStringLiteral("project");
    }
    return value.left(40);
}

} // namespace

EditorController::EditorController(QObject* parent)
    : QObject(parent), outliner_(std::make_unique<OutlinerModel>(session_, this)) {
    loadRecentProjects();
    autosaveTimer_.setInterval(60'000);
    autosaveTimer_.setTimerType(Qt::CoarseTimer);
    connect(&autosaveTimer_, &QTimer::timeout, this, [this] {
        if (session_.hasProject() && session_.isDirty()) {
            (void)autosaveNow();
        }
    });
    autosaveTimer_.start();
}

EditorController::~EditorController() = default;

QString EditorController::projectName() const {
    const auto* document = session_.document();
    return document ? QString::fromStdString(document->manifest.name) : QString{};
}

QString EditorController::projectPath() const {
    const auto* document = session_.document();
    return document ? fromFilesystemPath(document->root) : QString{};
}

int EditorController::sceneObjectCount() const {
    const auto* scene = session_.scene();
    return scene ? static_cast<int>(scene->size()) : 0;
}

QString EditorController::undoLabel() const {
    if (!session_.canUndo()) {
        return QStringLiteral("Undo");
    }
    return QStringLiteral("Undo %1").arg(QString::fromUtf8(session_.nextUndoName().data(),
                                                           static_cast<qsizetype>(session_.nextUndoName().size())));
}

QString EditorController::redoLabel() const {
    if (!session_.canRedo()) {
        return QStringLiteral("Redo");
    }
    return QStringLiteral("Redo %1").arg(QString::fromUtf8(session_.nextRedoName().data(),
                                                           static_cast<qsizetype>(session_.nextRedoName().size())));
}

QString EditorController::workspace() const {
    const auto value = m3d::workspaceName(session_.workspace());
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QStringList EditorController::workspaceNames() const {
    return {
        QStringLiteral("Layout"), QStringLiteral("Modeling"), QStringLiteral("Sculpt"),
        QStringLiteral("UV"), QStringLiteral("Paint"), QStringLiteral("Shading"),
        QStringLiteral("Animation"), QStringLiteral("Rigging"), QStringLiteral("Nodes"),
        QStringLiteral("Render")
    };
}

QAbstractItemModel* EditorController::outlinerModel() const noexcept {
    return outliner_.get();
}

bool EditorController::hasActiveObject() const {
    return activeObject() != nullptr;
}

QString EditorController::activeObjectName() const {
    const auto* object = activeObject();
    return object ? QString::fromStdString(object->name) : QString{};
}

QString EditorController::activeObjectType() const {
    const auto* object = activeObject();
    return object ? objectTypeName(object->type) : QString{};
}

double EditorController::positionX() const { const auto* object = activeObject(); return object ? object->localTransform.position.x : 0.0; }
double EditorController::positionY() const { const auto* object = activeObject(); return object ? object->localTransform.position.y : 0.0; }
double EditorController::positionZ() const { const auto* object = activeObject(); return object ? object->localTransform.position.z : 0.0; }
double EditorController::scaleX() const { const auto* object = activeObject(); return object ? object->localTransform.scale.x : 1.0; }
double EditorController::scaleY() const { const auto* object = activeObject(); return object ? object->localTransform.scale.y : 1.0; }
double EditorController::scaleZ() const { const auto* object = activeObject(); return object ? object->localTransform.scale.z : 1.0; }

QString EditorController::transformTool() const {
    switch (transformTool_) {
    case m3d::TransformTool::Translate: return QStringLiteral("Move");
    case m3d::TransformTool::Rotate: return QStringLiteral("Rotate");
    case m3d::TransformTool::Scale: return QStringLiteral("Scale");
    }
    return QStringLiteral("Move");
}

QString EditorController::transformSpace() const {
    return transformSpace_ == m3d::TransformSpace::Local
        ? QStringLiteral("Local") : QStringLiteral("Global");
}

QString EditorController::pivotMode() const {
    switch (pivotMode_) {
    case m3d::PivotMode::Median: return QStringLiteral("Median");
    case m3d::PivotMode::Active: return QStringLiteral("Active");
    case m3d::PivotMode::IndividualOrigins: return QStringLiteral("Individual");
    }
    return QStringLiteral("Median");
}

QString EditorController::activeLayerName() const {
    const auto active = session_.activeLayer();
    if (!active) return QStringLiteral("All");
    const auto* scene = session_.scene();
    const auto* layer = scene ? scene->findLayer(*active) : nullptr;
    return layer ? QString::fromStdString(layer->name) : QStringLiteral("All");
}

QStringList EditorController::layerNames() const {
    QStringList values{QStringLiteral("All")};
    const auto* scene = session_.scene();
    if (!scene) return values;
    for (const auto& layer : scene->layers()) values.push_back(QString::fromStdString(layer.name));
    if (values.size() > 2) std::sort(values.begin() + 1, values.end());
    return values;
}

QStringList EditorController::collectionNames() const {
    QStringList values;
    const auto* scene = session_.scene();
    if (!scene) return values;
    for (const auto& collection : scene->collections()) values.push_back(QString::fromStdString(collection.name));
    std::sort(values.begin(), values.end());
    return values;
}

QString EditorController::meshSelectionMode() const {
    const auto* selection = session_.meshSelection();
    if (!selection) return QStringLiteral("Object");
    switch (selection->mode()) {
    case m3d::MeshSelectionMode::Vertex: return QStringLiteral("Vertex");
    case m3d::MeshSelectionMode::Edge: return QStringLiteral("Edge");
    case m3d::MeshSelectionMode::Face: return QStringLiteral("Face");
    }
    return QStringLiteral("Vertex");
}

QStringList EditorController::meshSelectionModes() const {
    return {QStringLiteral("Vertex"), QStringLiteral("Edge"), QStringLiteral("Face")};
}

int EditorController::selectedMeshElementCount() const {
    const auto* selection = session_.meshSelection();
    if (!selection) return 0;
    switch (selection->mode()) {
    case m3d::MeshSelectionMode::Vertex:
        return static_cast<int>(selection->selectedVertices().size());
    case m3d::MeshSelectionMode::Edge:
        return static_cast<int>(selection->selectedEdges().size());
    case m3d::MeshSelectionMode::Face:
        return static_cast<int>(selection->selectedFaces().size());
    }
    return 0;
}

m3d::TransformSnapSettings EditorController::transformSnapSettings() const noexcept {
    m3d::TransformSnapSettings settings;
    settings.translationEnabled = transformSnapEnabled_;
    settings.rotationEnabled = transformSnapEnabled_;
    settings.scaleEnabled = transformSnapEnabled_;
    return settings;
}

bool EditorController::createProject(const QString& name) {
    if (manipulator_.active()) (void)cancelViewportTransform();
    const QString cleaned = cleanProjectName(name);
    if (cleaned.isEmpty()) {
        setStatus(QStringLiteral("Project name cannot be empty."));
        return false;
    }

    const QString root = allocateProjectPath(cleaned);
    std::string error;
    if (!session_.createProject(toFilesystemPath(root), cleaned.toStdString(), &error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }

    rememberProject(root);
    setRecoveryAvailable(false);
    setStatus(QStringLiteral("Project created."));
    refreshUi();
    emit workspaceChanged();
    return true;
}

bool EditorController::openProject(const QString& path) {
    if (manipulator_.active()) (void)cancelViewportTransform();
    QString root = QDir::cleanPath(path.trimmed());
    if (root.isEmpty()) {
        setStatus(QStringLiteral("Project path cannot be empty."));
        return false;
    }
    if (QFileInfo(root).fileName() == QStringLiteral("project.m3dproj")) {
        root = QFileInfo(root).absolutePath();
    }

    std::string error;
    if (!session_.openProject(toFilesystemPath(root), &error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }

    rememberProject(root);
    setRecoveryAvailable(session_.hasAutosave());
    setStatus(QStringLiteral("Project opened."));
    refreshUi();
    emit workspaceChanged();
    return true;
}

bool EditorController::openRecent(int index) {
    if (index < 0 || index >= recentProjects_.size()) {
        return false;
    }
    return openProject(recentProjects_.at(index));
}

bool EditorController::saveProject() {
    std::string error;
    if (!session_.saveProject(&error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setRecoveryAvailable(false);
    setStatus(QStringLiteral("Project saved."));
    refreshUi();
    return true;
}

bool EditorController::autosaveNow() {
    if (!session_.hasProject() || !session_.isDirty()) {
        return true;
    }
    std::string error;
    if (!session_.writeAutosave(&error)) {
        setStatus(QStringLiteral("Autosave failed: %1").arg(QString::fromStdString(error)));
        return false;
    }
    setStatus(QStringLiteral("Autosaved."));
    return true;
}

bool EditorController::recoverAutosave() {
    if (manipulator_.active()) (void)cancelViewportTransform();
    std::string error;
    if (!session_.recoverAutosave(&error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setRecoveryAvailable(false);
    setStatus(QStringLiteral("Autosave recovered. Save the project to keep it."));
    refreshUi();
    return true;
}

bool EditorController::discardAutosave() {
    std::string error;
    if (!session_.discardAutosave(&error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setRecoveryAvailable(false);
    setStatus(QStringLiteral("Recovery autosave discarded."));
    return true;
}

void EditorController::closeProject() {
    if (manipulator_.active()) (void)cancelViewportTransform();
    if (session_.isDirty()) {
        (void)autosaveNow();
    }
    session_.closeProject();
    setRecoveryAvailable(false);
    setStatus(QStringLiteral("Project closed."));
    refreshUi();
    emit workspaceChanged();
}

bool EditorController::addObject(const QString& typeNameValue) {
    const auto type = objectTypeFromName(typeNameValue);
    if (!type) {
        setStatus(QStringLiteral("Unsupported object type."));
        return false;
    }
    const auto created = session_.createObject(*type, objectTypeName(*type).toStdString());
    if (!created) {
        setStatus(QStringLiteral("Could not create object."));
        return false;
    }
    setStatus(QStringLiteral("%1 object created.").arg(objectTypeName(*type)));
    refreshUi();
    return true;
}

bool EditorController::deleteSelection() {
    if (!session_.deleteSelection()) {
        return false;
    }
    setStatus(QStringLiteral("Selection deleted."));
    refreshUi();
    return true;
}

bool EditorController::duplicateSelection() {
    if (!session_.duplicateSelection()) return false;
    setStatus(QStringLiteral("Selection duplicated."));
    refreshUi();
    return true;
}

bool EditorController::setObjectVisible(const QString& objectId, bool visible) {
    const auto id = m3d::ObjectId::fromString(objectId.toStdString());
    if (!id || !session_.setObjectVisible(*id, visible)) return false;
    setStatus(visible ? QStringLiteral("Object shown.") : QStringLiteral("Object hidden."));
    refreshUi();
    return true;
}

bool EditorController::setObjectLocked(const QString& objectId, bool locked) {
    const auto id = m3d::ObjectId::fromString(objectId.toStdString());
    if (!id || !session_.setObjectLocked(*id, locked)) return false;
    setStatus(locked ? QStringLiteral("Object locked.") : QStringLiteral("Object unlocked."));
    refreshUi();
    return true;
}

bool EditorController::selectObject(const QString& objectId, bool toggle) {
    const auto parsed = m3d::ObjectId::fromString(objectId.toStdString());
    if (!parsed) {
        return false;
    }
    if (!session_.select(*parsed, toggle ? m3d::SelectionMode::Toggle : m3d::SelectionMode::Replace)) {
        return false;
    }
    outliner_->refresh();
    emit selectionChanged();
    return true;
}

void EditorController::clearSelection() {
    session_.clearSelection();
    outliner_->refresh();
    emit selectionChanged();
}

bool EditorController::renameActive(const QString& name) {
    const auto active = session_.selection().active();
    const QString cleaned = name.trimmed();
    const auto* object = activeObject();
    if (!active || !object || cleaned.isEmpty()) {
        return false;
    }
    if (object->name == cleaned.toStdString()) {
        return true;
    }
    if (!session_.renameObject(*active, cleaned.toStdString())) {
        return false;
    }
    refreshUi();
    return true;
}

bool EditorController::setActivePosition(double x, double y, double z) {
    const auto active = session_.selection().active();
    const auto* object = activeObject();
    if (!active || !object) {
        return false;
    }
    auto transform = object->localTransform;
    transform.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    if (transform == object->localTransform) {
        return true;
    }
    if (!session_.transformObject(*active, transform)) {
        return false;
    }
    refreshUi();
    return true;
}

bool EditorController::setActiveScale(double x, double y, double z) {
    const auto active = session_.selection().active();
    const auto* object = activeObject();
    if (!active || !object) {
        return false;
    }
    auto transform = object->localTransform;
    transform.scale = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    if (transform == object->localTransform) {
        return true;
    }
    if (!session_.transformObject(*active, transform)) {
        return false;
    }
    refreshUi();
    return true;
}

bool EditorController::undo() {
    if (!session_.undo()) {
        return false;
    }
    setStatus(QStringLiteral("Undo."));
    refreshUi();
    return true;
}

bool EditorController::redo() {
    if (!session_.redo()) {
        return false;
    }
    setStatus(QStringLiteral("Redo."));
    refreshUi();
    return true;
}

bool EditorController::setWorkspace(const QString& name) {
    const auto value = workspaceFromName(name);
    if (!value) {
        return false;
    }
    session_.setWorkspace(*value);
    emit workspaceChanged();
    return true;
}

bool EditorController::setTransformTool(const QString& name) {
    const auto value = transformToolFromName(name);
    if (!value) return false;
    if (manipulator_.active()) (void)cancelViewportTransform();
    if (transformTool_ == *value) return true;
    transformTool_ = *value;
    emit transformSettingsChanged();
    return true;
}

bool EditorController::setTransformSpace(const QString& name) {
    const auto value = transformSpaceFromName(name);
    if (!value) return false;
    if (manipulator_.active()) (void)cancelViewportTransform();
    if (transformSpace_ == *value) return true;
    transformSpace_ = *value;
    emit transformSettingsChanged();
    return true;
}

bool EditorController::setPivotMode(const QString& name) {
    const auto value = pivotModeFromName(name);
    if (!value) return false;
    if (manipulator_.active()) (void)cancelViewportTransform();
    if (pivotMode_ == *value) return true;
    pivotMode_ = *value;
    emit transformSettingsChanged();
    return true;
}

bool EditorController::setActiveLayer(const QString& name) {
    if (!session_.hasProject() || manipulator_.active()) return false;
    const QString cleaned = name.trimmed();
    if (cleaned.compare(QStringLiteral("All"), Qt::CaseInsensitive) == 0) {
        if (!session_.setActiveLayer(std::nullopt)) return false;
        setStatus(QStringLiteral("Layer: All"));
        outliner_->refresh();
        emit selectionChanged();
        emit layerChanged();
        return true;
    }
    const auto* scene = session_.scene();
    if (!scene) return false;
    for (const auto& layer : scene->layers()) {
        if (QString::fromStdString(layer.name).compare(cleaned, Qt::CaseInsensitive) != 0) continue;
        if (!session_.setActiveLayer(layer.id)) return false;
        setStatus(QStringLiteral("Layer: %1").arg(QString::fromStdString(layer.name)));
        outliner_->refresh();
        emit selectionChanged();
        emit layerChanged();
        return true;
    }
    return false;
}

bool EditorController::createCollection() {
    auto* scene = session_.scene();
    if (!scene || manipulator_.active()) return false;
    int suffix = static_cast<int>(scene->collectionCount()) + 1;
    std::string name;
    for (;;) {
        name = "Collection " + std::to_string(suffix++);
        const auto duplicate = std::any_of(scene->collections().cbegin(), scene->collections().cend(), [&name](const m3d::SceneCollection& item) {
            return item.name == name;
        });
        if (!duplicate) break;
    }
    if (!session_.createCollection(name)) return false;
    setStatus(QStringLiteral("Collection created."));
    refreshUi();
    return true;
}

bool EditorController::createLayer() {
    auto* scene = session_.scene();
    if (!scene || manipulator_.active()) return false;
    int suffix = static_cast<int>(scene->layerCount()) + 1;
    std::string name;
    for (;;) {
        name = "Layer " + std::to_string(suffix++);
        const auto duplicate = std::any_of(scene->layers().cbegin(), scene->layers().cend(), [&name](const m3d::SceneLayer& item) {
            return item.name == name;
        });
        if (!duplicate) break;
    }
    const auto created = session_.createLayer(name);
    if (!created) return false;
    (void)session_.setActiveLayer(*created);
    setStatus(QStringLiteral("Layer created."));
    refreshUi();
    emit layerChanged();
    return true;
}

bool EditorController::addSelectionToCollection(const QString& collectionName) {
    auto* scene = session_.scene();
    if (!scene) return false;
    for (const auto& collection : scene->collections()) {
        if (QString::fromStdString(collection.name) != collectionName) continue;
        if (!session_.addSelectionToCollection(collection.id)) return false;
        setStatus(QStringLiteral("Selection added to %1.").arg(collectionName));
        refreshUi();
        return true;
    }
    return false;
}

bool EditorController::addCollectionToLayer(const QString& collectionName, const QString& layerName) {
    auto* scene = session_.scene();
    if (!scene || layerName == QStringLiteral("All")) return false;
    std::optional<m3d::CollectionId> collectionId;
    std::optional<m3d::LayerId> layerId;
    for (const auto& collection : scene->collections()) {
        if (QString::fromStdString(collection.name) == collectionName) collectionId = collection.id;
    }
    for (const auto& layer : scene->layers()) {
        if (QString::fromStdString(layer.name) == layerName) layerId = layer.id;
    }
    if (!collectionId || !layerId || !session_.addCollectionToLayer(*layerId, *collectionId)) return false;
    setStatus(QStringLiteral("Collection linked to layer."));
    refreshUi();
    emit layerChanged();
    return true;
}

bool EditorController::toggleCollectionVisible(const QString& collectionName) {
    auto* scene = session_.scene();
    if (!scene) return false;
    for (const auto& collection : scene->collections()) {
        if (QString::fromStdString(collection.name) != collectionName) continue;
        if (!session_.setCollectionVisible(collection.id, !collection.visible)) return false;
        refreshUi();
        return true;
    }
    return false;
}

bool EditorController::toggleCollectionLocked(const QString& collectionName) {
    auto* scene = session_.scene();
    if (!scene) return false;
    for (const auto& collection : scene->collections()) {
        if (QString::fromStdString(collection.name) != collectionName) continue;
        if (!session_.setCollectionLocked(collection.id, !collection.locked)) return false;
        refreshUi();
        return true;
    }
    return false;
}

void EditorController::setTransformSnapEnabled(bool enabled) {
    if (manipulator_.active()) (void)cancelViewportTransform();
    if (transformSnapEnabled_ == enabled) return;
    transformSnapEnabled_ = enabled;
    emit transformSettingsChanged();
}

bool EditorController::toggleEditMode() {
    if (manipulator_.active()) (void)cancelViewportTransform();
    if (session_.hasMeshEditTransaction()) return commitEditMode();
    const auto active = session_.selection().active();
    if (!active) {
        setStatus(QStringLiteral("Select a mesh object before entering Edit Mode."));
        return false;
    }
    std::string error;
    if (!session_.beginMeshEdit(*active, &error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    session_.setWorkspace(m3d::Workspace::Modeling);
    setStatus(QStringLiteral("Edit Mode • Vertex selection"));
    refreshUi();
    emit workspaceChanged();
    emit editModeChanged();
    return true;
}

bool EditorController::commitEditMode() {
    std::string error;
    if (!session_.commitMeshEdit("Edit Mesh", &error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setStatus(QStringLiteral("Mesh edit committed."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::cancelEditMode() {
    if (!session_.cancelMeshEdit()) return false;
    setStatus(QStringLiteral("Mesh edit cancelled."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::setMeshSelectionMode(const QString& name) {
    if (!session_.hasMeshEditTransaction()) return false;
    const QString value = name.trimmed().toLower();
    std::optional<m3d::MeshSelectionMode> mode;
    if (value == QStringLiteral("vertex")) mode = m3d::MeshSelectionMode::Vertex;
    else if (value == QStringLiteral("edge")) mode = m3d::MeshSelectionMode::Edge;
    else if (value == QStringLiteral("face")) mode = m3d::MeshSelectionMode::Face;
    if (!mode || !session_.setMeshSelectionMode(*mode)) return false;
    setStatus(QStringLiteral("Edit Mode • %1 selection").arg(name));
    emit editModeChanged();
    emit selectionChanged();
    return true;
}

bool EditorController::selectMeshElement(const QString& type, int id, bool toggle) {
    if (!session_.hasMeshEditTransaction() || id <= 0) return false;
    const auto action = toggle ? m3d::MeshSelectionAction::Toggle : m3d::MeshSelectionAction::Replace;
    const QString value = type.trimmed().toLower();
    bool selected = false;
    if (value == QStringLiteral("vertex")) {
        selected = session_.selectMeshVertex(m3d::EditableVertexId{static_cast<std::uint32_t>(id)}, action);
    } else if (value == QStringLiteral("edge")) {
        selected = session_.selectMeshEdge(m3d::EditableEdgeId{static_cast<std::uint32_t>(id)}, action);
    } else if (value == QStringLiteral("face")) {
        selected = session_.selectMeshFace(m3d::EditableFaceId{static_cast<std::uint32_t>(id)}, action);
    }
    if (!selected) return false;
    emit editModeChanged();
    emit selectionChanged();
    return true;
}

bool EditorController::clearMeshSelection() {
    if (!session_.clearMeshSelection()) return false;
    emit editModeChanged();
    emit selectionChanged();
    return true;
}

bool EditorController::extrudeSelectedFace(double distance) {
    std::string error;
    if (!session_.extrudeSelectedMeshFace(static_cast<float>(distance), &error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setStatus(QStringLiteral("Face extruded."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::insetSelectedFace(double ratio) {
    std::string error;
    if (!session_.insetSelectedMeshFace(static_cast<float>(ratio), &error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setStatus(QStringLiteral("Face inset."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::subdivideSelectedFace() {
    std::string error;
    if (!session_.subdivideSelectedMeshFace(&error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setStatus(QStringLiteral("Face subdivided."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::mergeSelectedVertices() {
    std::string error;
    if (!session_.mergeSelectedMeshVertices(&error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setStatus(QStringLiteral("Vertices merged to active."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::weldSelectedVertices(double distance) {
    std::string error;
    if (!session_.weldSelectedMeshVertices(static_cast<float>(distance), &error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setStatus(QStringLiteral("Selected vertices welded."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::fillSelectedBoundary() {
    std::string error;
    if (!session_.fillSelectedMeshBoundary(&error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setStatus(QStringLiteral("Boundary loop filled."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::bridgeSelectedBoundaries() {
    std::string error;
    if (!session_.bridgeSelectedMeshBoundaries(&error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setStatus(QStringLiteral("Boundary loops bridged."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::loopCutSelectedEdge() {
    std::string error;
    if (!session_.loopCutSelectedMeshEdge(&error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setStatus(QStringLiteral("Centered quad-ring Loop Cut created."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::deleteSelectedMeshElements() {
    std::string error;
    if (!session_.deleteSelectedMeshElements(&error)) {
        setStatus(QString::fromStdString(error));
        return false;
    }
    setStatus(QStringLiteral("Selected mesh elements deleted."));
    refreshUi();
    emit editModeChanged();
    return true;
}

bool EditorController::beginViewportTransform(m3d::TransformConstraint constraint) {
    bool started = false;
    const auto snapping = transformSnapSettings();
    switch (transformTool_) {
    case m3d::TransformTool::Translate:
        started = manipulator_.beginTranslate(session_, transformSpace_, constraint, snapping);
        break;
    case m3d::TransformTool::Rotate:
        started = manipulator_.beginRotate(session_, transformSpace_, constraint, pivotMode_, snapping);
        break;
    case m3d::TransformTool::Scale:
        started = manipulator_.beginScale(session_, transformSpace_, constraint, pivotMode_, snapping);
        break;
    }
    if (!started) return false;
    setStatus(QStringLiteral("%1 transform started.").arg(transformTool()));
    emit transformActivityChanged();
    emit historyChanged();
    return true;
}

bool EditorController::updateViewportTranslation(m3d::Vec3 gizmoComponents) {
    if (transformTool_ != m3d::TransformTool::Translate ||
        !manipulator_.updateTranslation(gizmoComponents)) return false;
    refreshTransformPreview();
    return true;
}

bool EditorController::updateViewportRotation(float angleRadians) {
    if (transformTool_ != m3d::TransformTool::Rotate ||
        !manipulator_.updateRotation(angleRadians)) return false;
    refreshTransformPreview();
    return true;
}

bool EditorController::updateViewportScale(float factor) {
    if (transformTool_ != m3d::TransformTool::Scale ||
        !manipulator_.updateScale(factor)) return false;
    refreshTransformPreview();
    return true;
}

bool EditorController::commitViewportTransform() {
    if (!manipulator_.active()) return false;
    if (!manipulator_.commit()) return false;
    setStatus(QStringLiteral("Transform committed."));
    emit transformActivityChanged();
    refreshUi();
    return true;
}

bool EditorController::cancelViewportTransform() {
    if (!manipulator_.active()) return false;
    if (!manipulator_.cancel()) return false;
    setStatus(QStringLiteral("Transform cancelled."));
    emit transformActivityChanged();
    refreshUi();
    return true;
}

void EditorController::handleApplicationState(Qt::ApplicationState state) {
    if (state != Qt::ApplicationActive && session_.hasProject() && session_.isDirty()) {
        (void)autosaveNow();
    }
}

const m3d::SceneObject* EditorController::activeObject() const {
    const auto active = session_.selection().active();
    const auto* scene = session_.scene();
    return active && scene ? scene->find(*active) : nullptr;
}

QString EditorController::objectTypeName(m3d::ObjectType type) {
    switch (type) {
    case m3d::ObjectType::Mesh: return QStringLiteral("Mesh");
    case m3d::ObjectType::Camera: return QStringLiteral("Camera");
    case m3d::ObjectType::Light: return QStringLiteral("Light");
    case m3d::ObjectType::Empty: return QStringLiteral("Empty");
    case m3d::ObjectType::Curve: return QStringLiteral("Curve");
    case m3d::ObjectType::Text: return QStringLiteral("Text");
    case m3d::ObjectType::Armature: return QStringLiteral("Armature");
    case m3d::ObjectType::Volume: return QStringLiteral("Volume");
    case m3d::ObjectType::Image: return QStringLiteral("Image");
    case m3d::ObjectType::Reference: return QStringLiteral("Reference");
    case m3d::ObjectType::Collection: return QStringLiteral("Collection");
    }
    return QStringLiteral("Object");
}

std::optional<m3d::ObjectType> EditorController::objectTypeFromName(const QString& name) {
    const QString value = name.trimmed().toLower();
    if (value == QStringLiteral("mesh")) return m3d::ObjectType::Mesh;
    if (value == QStringLiteral("camera")) return m3d::ObjectType::Camera;
    if (value == QStringLiteral("light")) return m3d::ObjectType::Light;
    if (value == QStringLiteral("empty")) return m3d::ObjectType::Empty;
    if (value == QStringLiteral("curve")) return m3d::ObjectType::Curve;
    if (value == QStringLiteral("text")) return m3d::ObjectType::Text;
    if (value == QStringLiteral("armature")) return m3d::ObjectType::Armature;
    if (value == QStringLiteral("volume")) return m3d::ObjectType::Volume;
    if (value == QStringLiteral("image")) return m3d::ObjectType::Image;
    if (value == QStringLiteral("reference")) return m3d::ObjectType::Reference;
    if (value == QStringLiteral("collection")) return m3d::ObjectType::Collection;
    return std::nullopt;
}

std::optional<m3d::TransformTool> EditorController::transformToolFromName(const QString& name) {
    const QString value = name.trimmed().toLower();
    if (value == QStringLiteral("move") || value == QStringLiteral("translate"))
        return m3d::TransformTool::Translate;
    if (value == QStringLiteral("rotate")) return m3d::TransformTool::Rotate;
    if (value == QStringLiteral("scale")) return m3d::TransformTool::Scale;
    return std::nullopt;
}

std::optional<m3d::TransformSpace> EditorController::transformSpaceFromName(const QString& name) {
    const QString value = name.trimmed().toLower();
    if (value == QStringLiteral("global")) return m3d::TransformSpace::Global;
    if (value == QStringLiteral("local")) return m3d::TransformSpace::Local;
    return std::nullopt;
}

std::optional<m3d::PivotMode> EditorController::pivotModeFromName(const QString& name) {
    const QString value = name.trimmed().toLower();
    if (value == QStringLiteral("median")) return m3d::PivotMode::Median;
    if (value == QStringLiteral("active")) return m3d::PivotMode::Active;
    if (value == QStringLiteral("individual") || value == QStringLiteral("individual origins"))
        return m3d::PivotMode::IndividualOrigins;
    return std::nullopt;
}

std::optional<m3d::Workspace> EditorController::workspaceFromName(const QString& name) {
    const QString value = name.trimmed().toLower();
    if (value == QStringLiteral("layout")) return m3d::Workspace::Layout;
    if (value == QStringLiteral("modeling")) return m3d::Workspace::Modeling;
    if (value == QStringLiteral("sculpt")) return m3d::Workspace::Sculpt;
    if (value == QStringLiteral("uv")) return m3d::Workspace::UV;
    if (value == QStringLiteral("paint")) return m3d::Workspace::Paint;
    if (value == QStringLiteral("shading")) return m3d::Workspace::Shading;
    if (value == QStringLiteral("animation")) return m3d::Workspace::Animation;
    if (value == QStringLiteral("rigging")) return m3d::Workspace::Rigging;
    if (value == QStringLiteral("nodes")) return m3d::Workspace::Nodes;
    if (value == QStringLiteral("render")) return m3d::Workspace::Render;
    return std::nullopt;
}

std::filesystem::path EditorController::toFilesystemPath(const QString& value) {
    const QByteArray utf8 = value.toUtf8();
    return std::filesystem::path(utf8.constData());
}

QString EditorController::fromFilesystemPath(const std::filesystem::path& value) {
    return QString::fromUtf8(value.generic_string().c_str());
}

void EditorController::refreshUi() {
    outliner_->refresh();
    emit projectStateChanged();
    emit historyChanged();
    emit selectionChanged();
    emit editModeChanged();
}

void EditorController::refreshTransformPreview() {
    emit selectionChanged();
    emit historyChanged();
}

void EditorController::setStatus(QString message) {
    if (statusMessage_ == message) {
        return;
    }
    statusMessage_ = std::move(message);
    emit statusMessageChanged();
}

void EditorController::setRecoveryAvailable(bool value) {
    if (recoveryAvailable_ == value) {
        return;
    }
    recoveryAvailable_ = value;
    emit recoveryAvailableChanged();
}

void EditorController::loadRecentProjects() {
    recentProjects_ = settings_.value(QStringLiteral("recentProjects")).toStringList();
    recentProjects_.removeIf([](const QString& path) {
        return !QFileInfo(QDir(path).filePath(QStringLiteral("project.m3dproj"))).isFile();
    });
    settings_.setValue(QStringLiteral("recentProjects"), recentProjects_);
}

void EditorController::rememberProject(const QString& path) {
    const QString clean = QDir::cleanPath(path);
    recentProjects_.removeAll(clean);
    recentProjects_.prepend(clean);
    while (recentProjects_.size() > 12) {
        recentProjects_.removeLast();
    }
    settings_.setValue(QStringLiteral("recentProjects"), recentProjects_);
    emit recentProjectsChanged();
}

QString EditorController::allocateProjectPath(const QString& name) const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/Mobile3DStudio");
    }
    QDir baseDir(base);
    (void)baseDir.mkpath(QStringLiteral("projects"));
    const QString projectsRoot = baseDir.filePath(QStringLiteral("projects"));
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    return QDir(projectsRoot).filePath(slugFromName(name) + QLatin1Char('-') + suffix);
}
