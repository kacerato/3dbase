from pathlib import Path

def replace_once(path, old, new):
    p=Path(path); s=p.read_text(); c=s.count(old)
    if c!=1: raise SystemExit(f'{path}: expected 1 match, got {c}')
    p.write_text(s.replace(old,new,1))

# EditorSession active layer API/state.
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] const SelectionModel& selection() const noexcept { return selection_; }
    [[nodiscard]] Workspace workspace() const noexcept { return workspace_; }
''',
'''    [[nodiscard]] const SelectionModel& selection() const noexcept { return selection_; }
    [[nodiscard]] std::optional<LayerId> activeLayer() const noexcept { return activeLayer_; }
    [[nodiscard]] bool setActiveLayer(std::optional<LayerId> layer);
    [[nodiscard]] Workspace workspace() const noexcept { return workspace_; }
''')
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    SelectionModel selection_;
    std::optional<TransformTransactionState> transformTransaction_;
''',
'''    SelectionModel selection_;
    std::optional<LayerId> activeLayer_;
    std::optional<TransformTransactionState> transformTransaction_;
''')

# Helper for organization lock and active layer setter.
replace_once('src/editor/src/editor_session.cpp',
'''bool EditorSession::isDirty() const noexcept {
    return recoveredDirty_ || commands_.isDirty() || transformTransactionHasChanges();
}
''',
'''bool EditorSession::isDirty() const noexcept {
    return recoveredDirty_ || commands_.isDirty() || transformTransactionHasChanges();
}

bool EditorSession::setActiveLayer(std::optional<LayerId> layer) {
    if (transformTransaction_ || !document_) return false;
    if (layer && !document_->scene.containsLayer(*layer)) return false;
    if (activeLayer_ == layer) return true;
    activeLayer_ = layer;
    selection_.prune(document_->scene);
    ++selectionRevision_;
    ++sceneRevision_;
    return true;
}
''')
replace_once('src/editor/src/editor_session.cpp',
'''void EditorSession::closeProject() noexcept {
    transformTransaction_.reset();
''',
'''void EditorSession::closeProject() noexcept {
    transformTransaction_.reset();
    activeLayer_.reset();
''')
replace_once('src/editor/src/editor_session.cpp',
'''void EditorSession::resetForDocument(bool recoveredDirty) noexcept {
    transformTransaction_.reset();
''',
'''void EditorSession::resetForDocument(bool recoveredDirty) noexcept {
    transformTransaction_.reset();
    activeLayer_.reset();
''')

# Organization lock replaces authored lock checks in mutation paths.
for old,new in [
('''    const auto* target = document_->scene.find(object);
    if (!target || target->locked) return false;''','''    const auto* target = document_->scene.find(object);
    if (!target || document_->scene.isObjectLockedByOrganization(object, activeLayer_)) return false;'''),
('''        const auto* object = document_->scene.find(id);
        if (!object || object->locked) return false;''','''        const auto* object = document_->scene.find(id);
        if (!object || document_->scene.isObjectLockedByOrganization(id, activeLayer_)) return false;'''),
('''    const auto* current = document_->scene.find(object);
    if (!current || current->locked) return false;''','''    const auto* current = document_->scene.find(object);
    if (!current || document_->scene.isObjectLockedByOrganization(object, activeLayer_)) return false;'''),
('''        const auto* object = document_->scene.find(objectId);
        if (!object || object->locked) return false;''','''        const auto* object = document_->scene.find(objectId);
        if (!object || document_->scene.isObjectLockedByOrganization(objectId, activeLayer_)) return false;'''),
]:
    s=Path('src/editor/src/editor_session.cpp').read_text()
    if old in s: replace_once('src/editor/src/editor_session.cpp',old,new)

# Snapshot builder accepts layer and uses effective visibility/lock.
replace_once('src/render/include/mobile3d/render/render_snapshot.hpp',
'''                                                   std::uint64_t sceneRevision,
                                                   std::uint64_t selectionRevision);''',
'''                                                   std::uint64_t sceneRevision,
                                                   std::uint64_t selectionRevision,
                                                   std::optional<LayerId> activeLayer = std::nullopt);''')
replace_once('src/render/src/render_snapshot.cpp',
'''                                                 std::uint64_t sceneRevision,
                                                 std::uint64_t selectionRevision) {''',
'''                                                 std::uint64_t sceneRevision,
                                                 std::uint64_t selectionRevision,
                                                 std::optional<LayerId> activeLayer) {''')
replace_once('src/render/src/render_snapshot.cpp',
'''            .visible = object.visible,
            .locked = object.locked,
''',
'''            .visible = scene.isObjectVisibleInLayer(object.id, activeLayer),
            .locked = scene.isObjectLockedByOrganization(object.id, activeLayer),
''')

# Controller snapshot passes active layer and exposes selector.
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_PROPERTY(QString pivotMode READ pivotMode NOTIFY transformSettingsChanged)
    Q_PROPERTY(bool transformSnapEnabled READ transformSnapEnabled NOTIFY transformSettingsChanged)
''',
'''    Q_PROPERTY(QString pivotMode READ pivotMode NOTIFY transformSettingsChanged)
    Q_PROPERTY(QString activeLayerName READ activeLayerName NOTIFY layerChanged)
    Q_PROPERTY(QStringList layerNames READ layerNames NOTIFY projectStateChanged)
    Q_PROPERTY(bool transformSnapEnabled READ transformSnapEnabled NOTIFY transformSettingsChanged)
''')
replace_once('src/app/qt/editor_controller.hpp',
'''    [[nodiscard]] QString pivotMode() const;
    [[nodiscard]] bool transformSnapEnabled() const noexcept { return transformSnapEnabled_; }
''',
'''    [[nodiscard]] QString pivotMode() const;
    [[nodiscard]] QString activeLayerName() const;
    [[nodiscard]] QStringList layerNames() const;
    [[nodiscard]] bool transformSnapEnabled() const noexcept { return transformSnapEnabled_; }
''')
replace_once('src/app/qt/editor_controller.hpp',
'''        return m3d::RenderSnapshotBuilder::build(*scene, session_.selection(),
                                                 session_.sceneRevision(),
                                                 session_.selectionRevision());''',
'''        return m3d::RenderSnapshotBuilder::build(*scene, session_.selection(),
                                                 session_.sceneRevision(),
                                                 session_.selectionRevision(),
                                                 session_.activeLayer());''')
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool setPivotMode(const QString& name);
    Q_INVOKABLE void setTransformSnapEnabled(bool enabled);
''',
'''    Q_INVOKABLE bool setPivotMode(const QString& name);
    Q_INVOKABLE bool setActiveLayer(const QString& name);
    Q_INVOKABLE void setTransformSnapEnabled(bool enabled);
''')
replace_once('src/app/qt/editor_controller.hpp',
'''    void transformSettingsChanged();
    void transformActivityChanged();
''',
'''    void transformSettingsChanged();
    void transformActivityChanged();
    void layerChanged();
''')

replace_once('src/app/qt/editor_controller.cpp',
'''QString EditorController::pivotMode() const {
''',
'''QString EditorController::activeLayerName() const {
    const auto layer = session_.activeLayer();
    if (!layer) return QStringLiteral("All");
    const auto* scene = session_.scene();
    const auto* value = scene ? scene->findLayer(*layer) : nullptr;
    return value ? QString::fromStdString(value->name) : QStringLiteral("All");
}

QStringList EditorController::layerNames() const {
    QStringList result{QStringLiteral("All")};
    const auto* scene = session_.scene();
    if (!scene) return result;
    for (const auto& layer : scene->layers()) result.push_back(QString::fromStdString(layer.name));
    result.sort(Qt::CaseInsensitive);
    result.removeAll(QStringLiteral("All"));
    result.prepend(QStringLiteral("All"));
    return result;
}

QString EditorController::pivotMode() const {
''')
replace_once('src/app/qt/editor_controller.cpp',
'''void EditorController::setTransformSnapEnabled(bool enabled) {
''',
'''bool EditorController::setActiveLayer(const QString& name) {
    if (!session_.hasProject() || manipulator_.active()) return false;
    const QString target = name.trimmed();
    if (target.compare(QStringLiteral("All"), Qt::CaseInsensitive) == 0) {
        if (!session_.setActiveLayer(std::nullopt)) return false;
        setStatus(QStringLiteral("Showing all collections."));
        emit layerChanged();
        refreshUi();
        return true;
    }
    const auto* scene = session_.scene();
    if (!scene) return false;
    for (const auto& layer : scene->layers()) {
        if (QString::fromStdString(layer.name).compare(target, Qt::CaseInsensitive) != 0) continue;
        if (!session_.setActiveLayer(layer.id)) return false;
        setStatus(QStringLiteral("Layer: %1").arg(QString::fromStdString(layer.name)));
        emit layerChanged();
        refreshUi();
        return true;
    }
    return false;
}

void EditorController::setTransformSnapEnabled(bool enabled) {
''')

# Viewport toolbar layer selector (simple cycling button, no complex popup).
replace_once('src/app/qml/ViewportPlaceholder.qml',
'''        Button {
            height: 36
            text: root.controller.transformSnapEnabled ? "Snap On" : "Snap Off"
''',
'''        Button {
            height: 36
            text: "Layer: " + root.controller.activeLayerName
            enabled: !root.controller.transformInProgress
            onClicked: {
                const names = root.controller.layerNames
                const index = Math.max(0, names.indexOf(root.controller.activeLayerName))
                root.controller.setActiveLayer(names[(index + 1) % names.length])
            }
        }
        Button {
            height: 36
            text: root.controller.transformSnapEnabled ? "Snap On" : "Snap Off"
''')

# Tests active layer affects snapshot and collection lock blocks transaction.
p=Path('tests/test_render_snapshot.cpp'); s=p.read_text()
s += r'''

TEST_CASE("render snapshot applies active layer visibility and collection lock") {
    m3d::Scene scene;
    m3d::SelectionModel selection;
    const auto visible = scene.createObject(m3d::ObjectType::Empty, "Visible");
    const auto hidden = scene.createObject(m3d::ObjectType::Empty, "Hidden");
    const auto visibleCollection = scene.createCollection("Visible Collection");
    const auto hiddenCollection = scene.createCollection("Hidden Collection");
    const auto layer = scene.createLayer("Layer");
    REQUIRE(scene.addObjectToCollection(visibleCollection, visible));
    REQUIRE(scene.addObjectToCollection(hiddenCollection, hidden));
    REQUIRE(scene.addCollectionToLayer(layer, visibleCollection));
    REQUIRE(scene.setCollectionLocked(visibleCollection, true));
    const auto snapshot = m3d::RenderSnapshotBuilder::build(scene, selection, 1, 1, layer);
    REQUIRE(snapshot.find(visible)->visible);
    REQUIRE(snapshot.find(visible)->locked);
    REQUIRE(!snapshot.find(hidden)->visible);
}
'''; p.write_text(s)

p=Path('tests/test_editor_session.cpp'); s=p.read_text()
s += r'''

TEST_CASE("active layer collection lock blocks transform transactions") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Layer Lock", &error));
    const auto object = session.createObject(m3d::ObjectType::Empty, "Object");
    REQUIRE(object.has_value());
    auto* scene = session.scene();
    REQUIRE(scene != nullptr);
    const auto collection = scene->createCollection("Locked");
    const auto layer = scene->createLayer("Layer");
    REQUIRE(scene->addObjectToCollection(collection, *object));
    REQUIRE(scene->addCollectionToLayer(layer, collection));
    REQUIRE(scene->setCollectionLocked(collection, true));
    REQUIRE(session.setActiveLayer(layer));
    REQUIRE(session.select(*object, m3d::SelectionMode::Replace));
    REQUIRE(!session.beginTransformTransaction({*object}, "Move"));
    REQUIRE(session.setActiveLayer(std::nullopt));
    REQUIRE(!session.beginTransformTransaction({*object}, "Move"));
    REQUIRE(scene->setCollectionLocked(collection, false));
    REQUIRE(session.beginTransformTransaction({*object}, "Move"));
    REQUIRE(session.cancelTransformTransaction());
}
'''; p.write_text(s)

print('active scene layer integration applied')
