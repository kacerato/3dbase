from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")

source = "src/core/src/commands/object_commands.cpp"
replace_once(source, '#include <algorithm>\n#include <utility>\n',
             '#include <algorithm>\n#include <unordered_map>\n#include <unordered_set>\n#include <utility>\n')

replace_once(source,
'''RenameObjectCommand::RenameObjectCommand(Scene& scene, ObjectId object, std::string newName)
''',
'''DuplicateObjectsCommand::DuplicateObjectsCommand(Scene& scene, std::vector<ObjectId> objects)
    : scene_(scene), sources_(std::move(objects)) {}

bool DuplicateObjectsCommand::initialize() {
    if (sources_.empty()) return false;

    std::unordered_set<ObjectId, ObjectIdHash> uniqueSources;
    std::unordered_map<ObjectId, ObjectId, ObjectIdHash> objectMap;
    for (const auto sourceId : sources_) {
        if (!scene_.contains(sourceId) || !uniqueSources.insert(sourceId).second) return false;
        ObjectId duplicateId;
        do {
            duplicateId = ObjectId::generate();
        } while (scene_.contains(duplicateId) ||
                 std::any_of(mappings_.cbegin(), mappings_.cend(),
                             [duplicateId](const DuplicateObjectMapping& mapping) {
                                 return mapping.duplicate == duplicateId;
                             }));
        mappings_.push_back({sourceId, duplicateId});
        objectMap.emplace(sourceId, duplicateId);
    }

    std::unordered_map<ResourceId, ResourceId, ResourceIdHash> resourceMap;
    for (const auto sourceId : sources_) {
        const auto* sourceObject = scene_.find(sourceId);
        if (!sourceObject) return false;
        SceneObject duplicate = *sourceObject;
        duplicate.id = objectMap.at(sourceId);
        duplicate.name += " Copy";
        if (duplicate.parent) {
            const auto parentDuplicate = objectMap.find(*duplicate.parent);
            if (parentDuplicate != objectMap.end()) duplicate.parent = parentDuplicate->second;
        }

        if (duplicate.meshResource) {
            const ResourceId sourceResourceId = *duplicate.meshResource;
            auto mappedResource = resourceMap.find(sourceResourceId);
            if (mappedResource == resourceMap.end()) {
                const auto* sourceResource = scene_.findMeshResource(sourceResourceId);
                if (!sourceResource) return false;
                MeshResource copiedResource = *sourceResource;
                ResourceId duplicateResourceId;
                do {
                    duplicateResourceId = ResourceId::generate();
                } while (scene_.containsResource(duplicateResourceId) ||
                         std::any_of(resources_.cbegin(), resources_.cend(),
                                     [duplicateResourceId](const MeshResource& resource) {
                                         return resource.id == duplicateResourceId;
                                     }));
                copiedResource.id = duplicateResourceId;
                copiedResource.name += " Copy";
                std::string validationError;
                if (!copiedResource.validate(&validationError)) return false;
                resources_.push_back(std::move(copiedResource));
                mappedResource = resourceMap.emplace(sourceResourceId, duplicateResourceId).first;
            }
            duplicate.meshResource = mappedResource->second;
        }
        objects_.push_back(std::move(duplicate));
    }

    initialized_ = true;
    return true;
}

bool DuplicateObjectsCommand::insertPrepared() {
    std::vector<ResourceId> insertedResources;
    insertedResources.reserve(resources_.size());
    for (const auto& resource : resources_) {
        if (!scene_.insertMeshResource(resource)) {
            rollbackInserted();
            return false;
        }
        insertedResources.push_back(resource.id);
    }

    std::vector<bool> inserted(objects_.size(), false);
    std::size_t insertedCount = 0;
    while (insertedCount < objects_.size()) {
        bool progressed = false;
        for (std::size_t index = 0; index < objects_.size(); ++index) {
            if (inserted[index]) continue;
            const auto& object = objects_[index];
            if (object.parent && !scene_.contains(*object.parent)) continue;
            if (!scene_.insertObject(object)) {
                rollbackInserted();
                return false;
            }
            inserted[index] = true;
            ++insertedCount;
            progressed = true;
        }
        if (!progressed) {
            rollbackInserted();
            return false;
        }
    }
    return true;
}

void DuplicateObjectsCommand::rollbackInserted() noexcept {
    for (const auto& object : objects_) {
        if (scene_.contains(object.id)) (void)scene_.removeSubtree(object.id);
    }
    for (const auto& resource : resources_) {
        if (scene_.containsResource(resource.id)) (void)scene_.removeMeshResource(resource.id);
    }
}

bool DuplicateObjectsCommand::execute() {
    if (!initialized_ && !initialize()) return false;
    return insertPrepared();
}

bool DuplicateObjectsCommand::undo() {
    bool success = true;
    for (const auto& object : objects_) {
        if (!scene_.contains(object.id)) continue;
        const auto removed = scene_.removeSubtree(object.id);
        success = !removed.empty() && success;
    }
    for (const auto& resource : resources_) {
        if (!scene_.containsResource(resource.id)) continue;
        success = scene_.removeMeshResource(resource.id) && success;
    }
    return success;
}

RenameObjectCommand::RenameObjectCommand(Scene& scene, ObjectId object, std::string newName)
''')

# Editor API.
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool deleteObject(ObjectId object);
    [[nodiscard]] bool deleteSelection();
    [[nodiscard]] bool renameObject(ObjectId object, std::string name);
''',
'''    [[nodiscard]] bool deleteObject(ObjectId object);
    [[nodiscard]] bool deleteSelection();
    [[nodiscard]] bool duplicateSelection();
    [[nodiscard]] bool renameObject(ObjectId object, std::string name);
''')

replace_once('src/editor/src/editor_session.cpp',
'''bool EditorSession::renameObject(ObjectId object, std::string name) {
''',
'''bool EditorSession::duplicateSelection() {
    if (transformTransaction_ || !document_ || selection_.empty()) return false;
    const auto sources = selection_.selected();
    const auto previousActive = selection_.active();
    auto command = std::make_unique<DuplicateObjectsCommand>(document_->scene, sources);
    auto* duplicateCommand = command.get();
    if (!commands_.execute(std::move(command))) return false;

    const auto mappings = duplicateCommand->mappings();
    selection_.clear();
    bool selectedAny = false;
    std::optional<ObjectId> activeDuplicate;
    for (const auto& mapping : mappings) {
        if (previousActive && mapping.source == *previousActive) {
            activeDuplicate = mapping.duplicate;
            continue;
        }
        const auto mode = selectedAny ? SelectionMode::Add : SelectionMode::Replace;
        selectedAny = selection_.select(document_->scene, mapping.duplicate, mode) || selectedAny;
    }
    if (activeDuplicate) {
        const auto mode = selectedAny ? SelectionMode::Add : SelectionMode::Replace;
        selectedAny = selection_.select(document_->scene, *activeDuplicate, mode) || selectedAny;
    }
    if (!selectedAny && !mappings.empty()) {
        (void)selection_.select(document_->scene, mappings.front().duplicate, SelectionMode::Replace);
    }
    sceneMutated(false);
    ++selectionRevision_;
    return true;
}

bool EditorSession::renameObject(ObjectId object, std::string name) {
''')

# Controller API and implementation.
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool addObject(const QString& typeName);
    Q_INVOKABLE bool deleteSelection();
    Q_INVOKABLE bool selectObject(const QString& objectId, bool toggle = false);
''',
'''    Q_INVOKABLE bool addObject(const QString& typeName);
    Q_INVOKABLE bool deleteSelection();
    Q_INVOKABLE bool duplicateSelection();
    Q_INVOKABLE bool selectObject(const QString& objectId, bool toggle = false);
''')

replace_once('src/app/qt/editor_controller.cpp',
'''bool EditorController::selectObject(const QString& objectId, bool toggle) {
''',
'''bool EditorController::duplicateSelection() {
    if (!session_.duplicateSelection()) return false;
    setStatus(QStringLiteral("Selection duplicated."));
    refreshUi();
    return true;
}

bool EditorController::selectObject(const QString& objectId, bool toggle) {
''')

# Touch/desktop toolbar command.
replace_once('src/app/qml/TopBar.qml',
'''        ToolButton {
            id: addButton
            visible: root.controller.projectOpen
            text: "Add"
''',
'''        ToolButton {
            visible: root.controller.projectOpen && !root.compact
            enabled: root.controller.hasActiveObject && !root.controller.transformInProgress
            text: "Duplicate"
            onClicked: root.controller.duplicateSelection()
        }

        ToolButton {
            id: addButton
            visible: root.controller.projectOpen
            text: "Add"
''')

# Behavioral tests.
test = Path('tests/test_editor_session.cpp')
text = test.read_text(encoding='utf-8')
text += r'''

TEST_CASE("duplicate selection copies hierarchy and mesh resources in one undo step") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Duplicate Selection", &error));
    const auto parent = session.createObject(m3d::ObjectType::Mesh, "Parent Mesh");
    REQUIRE(parent.has_value());
    const auto child = session.createObject(m3d::ObjectType::Empty, "Child", *parent);
    REQUIRE(child.has_value());
    const auto originalResource = session.scene()->find(*parent)->meshResource;
    REQUIRE(originalResource.has_value());
    REQUIRE(session.select(*parent, m3d::SelectionMode::Replace));
    REQUIRE(session.select(*child, m3d::SelectionMode::Add));

    REQUIRE(session.duplicateSelection());
    REQUIRE(session.nextUndoName() == "Duplicate Selection");
    REQUIRE(session.scene()->size() == 4);
    REQUIRE(session.scene()->meshResources().size() == 2);
    REQUIRE(session.selection().size() == 2);

    std::optional<m3d::ObjectId> duplicateParent;
    std::optional<m3d::ObjectId> duplicateChild;
    for (const auto id : session.selection().selected()) {
        const auto* object = session.scene()->find(id);
        REQUIRE(object != nullptr);
        if (object->type == m3d::ObjectType::Mesh) duplicateParent = id;
        else if (object->name == "Child Copy") duplicateChild = id;
    }
    REQUIRE(duplicateParent.has_value());
    REQUIRE(duplicateChild.has_value());
    const auto* copiedParent = session.scene()->find(*duplicateParent);
    const auto* copiedChild = session.scene()->find(*duplicateChild);
    REQUIRE(copiedParent != nullptr);
    REQUIRE(copiedChild != nullptr);
    REQUIRE(copiedChild->parent == duplicateParent);
    REQUIRE(copiedParent->meshResource.has_value());
    REQUIRE(copiedParent->meshResource != originalResource);
    const auto* originalGeometry = session.scene()->findMeshResource(*originalResource);
    const auto* copiedGeometry = session.scene()->findMeshResource(*copiedParent->meshResource);
    REQUIRE(originalGeometry != nullptr);
    REQUIRE(copiedGeometry != nullptr);
    REQUIRE(copiedGeometry->vertices == originalGeometry->vertices);
    REQUIRE(copiedGeometry->indices == originalGeometry->indices);

    REQUIRE(session.undo());
    REQUIRE(session.scene()->size() == 2);
    REQUIRE(session.scene()->meshResources().size() == 1);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->size() == 4);
    REQUIRE(session.scene()->meshResources().size() == 2);
}

TEST_CASE("duplicate shared selected mesh resource is copied once for the duplicate group") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Shared Duplicate", &error));
    auto resource = m3d::MeshResource::makeCube("Shared", 1.0F);
    const auto resourceId = session.scene()->createMeshResource(resource);
    const auto first = session.scene()->createObject(m3d::ObjectType::Mesh, "First");
    const auto second = session.scene()->createObject(m3d::ObjectType::Mesh, "Second");
    REQUIRE(session.scene()->assignMesh(first, resourceId));
    REQUIRE(session.scene()->assignMesh(second, resourceId));
    REQUIRE(session.select(first, m3d::SelectionMode::Replace));
    REQUIRE(session.select(second, m3d::SelectionMode::Add));

    REQUIRE(session.duplicateSelection());
    REQUIRE(session.scene()->meshResources().size() == 2);
    std::optional<m3d::ResourceId> duplicateResource;
    for (const auto id : session.selection().selected()) {
        const auto* object = session.scene()->find(id);
        REQUIRE(object != nullptr);
        REQUIRE(object->meshResource.has_value());
        if (!duplicateResource) duplicateResource = object->meshResource;
        REQUIRE(object->meshResource == duplicateResource);
    }
    REQUIRE(duplicateResource.has_value());
    REQUIRE(*duplicateResource != resourceId);
}
'''
test.write_text(text, encoding='utf-8')

print("independent object duplication applied")
