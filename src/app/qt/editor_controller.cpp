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
    if (value.isEmpty()) return {};
    return value.left(80);
}

QString slugFromName(QString value) {
    value = value.trimmed().toLower();
    for (qsizetype index = 0; index < value.size(); ++index) {
        if (!value.at(index).isLetterOrNumber()) value[index] = QLatin1Char('_');
    }
    while (value.contains(QStringLiteral("__"))) value.replace(QStringLiteral("__"), QStringLiteral("_"));
    value = value.trimmed();
    if (value.isEmpty()) value = QStringLiteral("project");
    return value.left(40);
}

} // namespace

EditorController::EditorController(QObject* parent)
    : QObject(parent), outliner_(std::make_unique<OutlinerModel>(session_, this)) {
    loadRecentProjects();
    autosaveTimer_.setInterval(60'000);
    autosaveTimer_.setTimerType(Qt::CoarseTimer);
    connect(&autosaveTimer_, &QTimer::timeout, this, [this] {
        if (session_.hasProject() && session_.isDirty()) (void)autosaveNow();
    });
    autosaveTimer_.start();
}

EditorController::~EditorController() = default;

QString EditorController::projectName() const { const auto* document = session_.document(); return document ? QString::fromStdString(document->manifest.name) : QString{}; }
QString EditorController::projectPath() const { const auto* document = session_.document(); return document ? fromFilesystemPath(document->root) : QString{}; }
int EditorController::sceneObjectCount() const { const auto* scene = session_.scene(); return scene ? static_cast<int>(scene->size()) : 0; }

QString EditorController::undoLabel() const {
    if (!session_.canUndo()) return QStringLiteral("Undo");
    return QStringLiteral("Undo %1").arg(QString::fromUtf8(session_.nextUndoName().data(), static_cast<qsizetype>(session_.nextUndoName().size())));
}
QString EditorController::redoLabel() const {
    if (!session_.canRedo()) return QStringLiteral("Redo");
    return QStringLiteral("Redo %1").arg(QString::fromUtf8(session_.nextRedoName().data(), static_cast<qsizetype>(session_.nextRedoName().size())));
}
QString EditorController::workspace() const { const auto value = m3d::workspaceName(session_.workspace()); return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
QStringList EditorController::workspaceNames() const { return {QStringLiteral("Layout"), QStringLiteral("Modeling"), QStringLiteral("Sculpt"), QStringLiteral("UV"), QStringLiteral("Paint"), QStringLiteral("Shading"), QStringLiteral("Animation"), QStringLiteral("Rigging"), QStringLiteral("Nodes"), QStringLiteral("Render")}; }
QAbstractItemModel* EditorController::outlinerModel() const noexcept { return outliner_.get(); }
bool EditorController::hasActiveObject() const { return activeObject() != nullptr; }
QString EditorController::activeObjectName() const { const auto* object = activeObject(); return object ? QString::fromStdString(object->name) : QString{}; }
QString EditorController::activeObjectType() const { const auto* object = activeObject(); return object ? objectTypeName(object->type) : QString{}; }
double EditorController::positionX() const { const auto* object = activeObject(); return object ? object->localTransform.position.x : 0.0; }
double EditorController::positionY() const { const auto* object = activeObject(); return object ? object->localTransform.position.y : 0.0; }
double EditorController::positionZ() const { const auto* object = activeObject(); return object ? object->localTransform.position.z : 0.0; }
double EditorController::scaleX() const { const auto* object = activeObject(); return object ? object->localTransform.scale.x : 1.0; }
double EditorController::scaleY() const { const auto* object = activeObject(); return object ? object->localTransform.scale.y : 1.0; }
double EditorController::scaleZ() const { const auto* object = activeObject(); return object ? object->localTransform.scale.z : 1.0; }

bool EditorController::createProject(const QString& name) {
    const QString cleaned = cleanProjectName(name);
    if (cleaned.isEmpty()) { setStatus(QStringLiteral("Project name cannot be empty.")); return false; }
    const QString root = allocateProjectPath(cleaned);
    std::string error;
    if (!session_.createProject(toFilesystemPath(root), cleaned.toStdString(), &error)) { setStatus(QString::fromStdString(error)); return false; }
    rememberProject(root); setRecoveryAvailable(false); setStatus(QStringLiteral("Project created.")); refreshUi(); emit workspaceChanged(); return true;
}

bool EditorController::openProject(const QString& path) {
    QString root = QDir::cleanPath(path.trimmed());
    if (root.isEmpty()) { setStatus(QStringLiteral("Project path cannot be empty.")); return false; }
    if (QFileInfo(root).fileName() == QStringLiteral("project.m3dproj")) root = QFileInfo(root).absolutePath();
    std::string error;
    if (!session_.openProject(toFilesystemPath(root), &error)) { setStatus(QString::fromStdString(error)); return false; }
    rememberProject(root); setRecoveryAvailable(session_.hasAutosave()); setStatus(QStringLiteral("Project opened.")); refreshUi(); emit workspaceChanged(); return true;
}

bool EditorController::openRecent(int index) { if (index < 0 || index >= recentProjects_.size()) return false; return openProject(recentProjects_.at(index)); }

bool EditorController::saveProject() {
    std::string error;
    if (!session_.saveProject(&error)) { setStatus(QString::fromStdString(error)); return false; }
    setRecoveryAvailable(false); setStatus(QStringLiteral("Project saved.")); refreshUi(); return true;
}

bool EditorController::autosaveNow() {
    if (!session_.hasProject() || !session_.isDirty()) return true;
    std::string error;
    if (!session_.writeAutosave(&error)) { setStatus(QStringLiteral("Autosave failed: %1").arg(QString::fromStdString(error))); return false; }
    setStatus(QStringLiteral("Autosaved.")); return true;
}

bool EditorController::recoverAutosave() {
    std::string error;
    if (!session_.recoverAutosave(&error)) { setStatus(QString::fromStdString(error)); return false; }
    setRecoveryAvailable(false); setStatus(QStringLiteral("Autosave recovered. Save the project to keep it.")); refreshUi(); return true;
}

bool EditorController::discardAutosave() {
    std::string error;
    if (!session_.discardAutosave(&error)) { setStatus(QString::fromStdString(error)); return false; }
    setRecoveryAvailable(false); setStatus(QStringLiteral("Recovery autosave discarded.")); return true;
}

void EditorController::closeProject() {
    if (session_.isDirty()) (void)autosaveNow();
    session_.closeProject(); setRecoveryAvailable(false); setStatus(QStringLiteral("Project closed.")); refreshUi(); emit workspaceChanged();
}

bool EditorController::addObject(const QString& typeNameValue) {
    const auto type = objectTypeFromName(typeNameValue);
    if (!type) { setStatus(QStringLiteral("Unsupported object type.")); return false; }

    std::optional<m3d::ObjectId> created;
    if (*type == m3d::ObjectType::Mesh) {
        created = session_.createMeshObject(m3d::MeshResource::makeCube("Cube Geometry", 1.0F), "Cube");
    } else {
        created = session_.createObject(*type, objectTypeName(*type).toStdString());
    }
    if (!created) { setStatus(QStringLiteral("Could not create object.")); return false; }
    setStatus(QStringLiteral("%1 object created.").arg(objectTypeName(*type)));
    refreshUi();
    return true;
}

bool EditorController::deleteSelection() { if (!session_.deleteSelection()) return false; setStatus(QStringLiteral("Selection deleted.")); refreshUi(); return true; }

bool EditorController::selectObject(const QString& objectId, bool toggle) {
    const auto parsed = m3d::ObjectId::fromString(objectId.toStdString());
    if (!parsed) return false;
    if (!session_.select(*parsed, toggle ? m3d::SelectionMode::Toggle : m3d::SelectionMode::Replace)) return false;
    outliner_->refresh(); emit selectionChanged(); return true;
}

void EditorController::clearSelection() { session_.clearSelection(); outliner_->refresh(); emit selectionChanged(); }

bool EditorController::renameActive(const QString& name) {
    const auto active = session_.selection().active();
    const QString cleaned = name.trimmed();
    const auto* object = activeObject();
    if (!active || !object || cleaned.isEmpty()) return false;
    if (object->name == cleaned.toStdString()) return true;
    if (!session_.renameObject(*active, cleaned.toStdString())) return false;
    refreshUi(); return true;
}

bool EditorController::setActivePosition(double x, double y, double z) {
    const auto active = session_.selection().active(); const auto* object = activeObject();
    if (!active || !object) return false;
    auto transform = object->localTransform; transform.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    if (transform == object->localTransform) return true;
    if (!session_.transformObject(*active, transform)) return false;
    refreshUi(); return true;
}

bool EditorController::setActiveScale(double x, double y, double z) {
    const auto active = session_.selection().active(); const auto* object = activeObject();
    if (!active || !object) return false;
    auto transform = object->localTransform; transform.scale = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    if (transform == object->localTransform) return true;
    if (!session_.transformObject(*active, transform)) return false;
    refreshUi(); return true;
}

bool EditorController::undo() { if (!session_.undo()) return false; setStatus(QStringLiteral("Undo.")); refreshUi(); return true; }
bool EditorController::redo() { if (!session_.redo()) return false; setStatus(QStringLiteral("Redo.")); refreshUi(); return true; }

bool EditorController::setWorkspace(const QString& name) {
    const auto value = workspaceFromName(name); if (!value) return false; session_.setWorkspace(*value); emit workspaceChanged(); return true;
}

void EditorController::handleApplicationState(Qt::ApplicationState state) {
    if (state != Qt::ApplicationActive && session_.hasProject() && session_.isDirty()) (void)autosaveNow();
}

const m3d::SceneObject* EditorController::activeObject() const {
    const auto active = session_.selection().active(); const auto* scene = session_.scene(); return active && scene ? scene->find(*active) : nullptr;
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
    const QByteArray utf8 = value.toUtf8(); return std::filesystem::path(utf8.constData());
}
QString EditorController::fromFilesystemPath(const std::filesystem::path& value) { return QString::fromUtf8(value.generic_string().c_str()); }

QString EditorController::allocateProjectPath(const QString& name) const {
    const QString base = projectsRoot(); const QString slug = slugFromName(name);
    QString candidate = QDir(base).filePath(slug);
    int suffix = 2;
    while (QFileInfo::exists(candidate)) candidate = QDir(base).filePath(QStringLiteral("%1_%2").arg(slug).arg(suffix++));
    return candidate;
}

QString EditorController::projectsRoot() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.mobile3d");
    const QString projects = QDir(base).filePath(QStringLiteral("projects")); QDir().mkpath(projects); return projects;
}

void EditorController::loadRecentProjects() {
    QSettings settings; recentProjects_ = settings.value(QStringLiteral("projects/recent")).toStringList();
    recentProjects_.erase(std::remove_if(recentProjects_.begin(), recentProjects_.end(), [](const QString& path) { return !QFileInfo::exists(QDir(path).filePath(QStringLiteral("project.m3dproj"))); }), recentProjects_.end());
    recentProjects_ = recentProjects_.mid(0, 8); settings.setValue(QStringLiteral("projects/recent"), recentProjects_);
}

void EditorController::rememberProject(const QString& path) {
    recentProjects_.removeAll(path); recentProjects_.prepend(path); recentProjects_ = recentProjects_.mid(0, 8);
    QSettings settings; settings.setValue(QStringLiteral("projects/recent"), recentProjects_); emit recentProjectsChanged();
}

void EditorController::refreshUi() {
    outliner_->refresh();
    emit projectStateChanged(); emit historyChanged(); emit selectionChanged(); emit inspectorChanged();
}

void EditorController::setStatus(QString status) {
    if (status_ == status) return; status_ = std::move(status); emit statusMessageChanged();
}

void EditorController::setRecoveryAvailable(bool available) {
    if (recoveryAvailable_ == available) return; recoveryAvailable_ = available; emit recoveryAvailableChanged();
}

m3d::RenderSceneSnapshot EditorController::renderSnapshot() const {
    const auto* scene = session_.scene();
    if (!scene) return {};
    return m3d::RenderSnapshotBuilder::build(*scene, session_.selection(), session_.sceneRevision(), session_.selectionRevision());
}
