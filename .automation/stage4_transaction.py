from pathlib import Path

def replace_once(path, old, new):
    p=Path(path); s=p.read_text(); c=s.count(old)
    if c!=1: raise SystemExit(f'{path}: expected 1 match, got {c}')
    p.write_text(s.replace(old,new,1))

# Core command API
replace_once('src/core/include/mobile3d/core/commands/object_commands.hpp',
'''class ReparentObjectCommand final : public EditorCommand {''',
'''struct TransformChange final {
    ObjectId object{};
    Transform before{};
    Transform after{};
};

class TransformObjectsCommand final : public EditorCommand {
public:
    TransformObjectsCommand(Scene& scene, std::vector<TransformChange> changes,
                            std::string commandName = "Transform Objects");

    [[nodiscard]] std::string_view name() const noexcept override { return commandName_; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;

private:
    Scene& scene_;
    std::vector<TransformChange> changes_;
    std::string commandName_;
};

class ReparentObjectCommand final : public EditorCommand {''')

replace_once('src/core/src/commands/object_commands.cpp',
'''ReparentObjectCommand::ReparentObjectCommand(Scene& scene, ObjectId object,
                                             std::optional<ObjectId> newParent)''',
'''TransformObjectsCommand::TransformObjectsCommand(Scene& scene,
                                                     std::vector<TransformChange> changes,
                                                     std::string commandName)
    : scene_(scene), changes_(std::move(changes)), commandName_(std::move(commandName)) {
    if (commandName_.empty()) commandName_ = "Transform Objects";
}

bool TransformObjectsCommand::execute() {
    if (changes_.empty()) return false;
    for (std::size_t index = 0; index < changes_.size(); ++index) {
        if (!scene_.contains(changes_[index].object)) return false;
        for (std::size_t other = index + 1; other < changes_.size(); ++other) {
            if (changes_[index].object == changes_[other].object) return false;
        }
    }
    std::size_t applied = 0;
    for (; applied < changes_.size(); ++applied) {
        if (!scene_.setTransform(changes_[applied].object, changes_[applied].after)) break;
    }
    if (applied == changes_.size()) return true;
    while (applied > 0) {
        --applied;
        (void)scene_.setTransform(changes_[applied].object, changes_[applied].before);
    }
    return false;
}

bool TransformObjectsCommand::undo() {
    for (const auto& change : changes_) {
        if (!scene_.contains(change.object)) return false;
    }
    std::size_t applied = 0;
    for (; applied < changes_.size(); ++applied) {
        if (!scene_.setTransform(changes_[applied].object, changes_[applied].before)) break;
    }
    if (applied == changes_.size()) return true;
    while (applied > 0) {
        --applied;
        (void)scene_.setTransform(changes_[applied].object, changes_[applied].after);
    }
    return false;
}

ReparentObjectCommand::ReparentObjectCommand(Scene& scene, ObjectId object,
                                             std::optional<ObjectId> newParent)''')

# Editor session header
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp', '#include <string_view>\n', '#include <string_view>\n#include <vector>\n')
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool hasProject() const noexcept { return document_.has_value(); }
    [[nodiscard]] bool isDirty() const noexcept { return recoveredDirty_ || commands_.isDirty(); }
''',
'''    [[nodiscard]] bool hasProject() const noexcept { return document_.has_value(); }
    [[nodiscard]] bool isDirty() const noexcept;
''')
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool transformObject(ObjectId object, const Transform& transform);
    [[nodiscard]] bool reparentObject(ObjectId object, std::optional<ObjectId> parent);
''',
'''    [[nodiscard]] bool transformObject(ObjectId object, const Transform& transform);
    [[nodiscard]] bool beginTransformTransaction(const std::vector<ObjectId>& objects,
                                                 std::string commandName = "Transform Objects");
    [[nodiscard]] bool previewTransform(ObjectId object, const Transform& transform);
    [[nodiscard]] bool commitTransformTransaction();
    [[nodiscard]] bool cancelTransformTransaction();
    [[nodiscard]] bool hasTransformTransaction() const noexcept { return transformTransaction_.has_value(); }
    [[nodiscard]] bool reparentObject(ObjectId object, std::optional<ObjectId> parent);
''')
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''private:
    [[nodiscard]] bool requireProject(std::string* error) const;
    void resetForDocument(bool recoveredDirty) noexcept;
    void sceneMutated(bool pruneSelection = true);

    std::optional<ProjectDocument> document_;
''',
'''private:
    struct TransformTransactionState final {
        std::vector<TransformChange> changes;
        std::string commandName;
    };

    [[nodiscard]] bool requireProject(std::string* error) const;
    [[nodiscard]] bool transformTransactionHasChanges() const noexcept;
    void resetForDocument(bool recoveredDirty) noexcept;
    void sceneMutated(bool pruneSelection = true);

    std::optional<ProjectDocument> document_;
''')
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    SelectionModel selection_;
    Workspace workspace_{Workspace::Layout};
''',
'''    SelectionModel selection_;
    std::optional<TransformTransactionState> transformTransaction_;
    Workspace workspace_{Workspace::Layout};
''')
# Need TransformChange definition in header
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp', '#include "mobile3d/core/command_stack.hpp"\n', '#include "mobile3d/core/command_stack.hpp"\n#include "mobile3d/core/commands/object_commands.hpp"\n')

# Editor implementation behavior
replace_once('src/editor/src/editor_session.cpp',
'''void EditorSession::closeProject() noexcept {
    document_.reset();''',
'''void EditorSession::closeProject() noexcept {
    transformTransaction_.reset();
    document_.reset();''')
replace_once('src/editor/src/editor_session.cpp',
'''bool EditorSession::saveProject(std::string* error) {
    if (!requireProject(error)) return false;''',
'''bool EditorSession::saveProject(std::string* error) {
    if (!requireProject(error)) return false;
    if (transformTransaction_) {
        if (error) *error = "Cannot save during an active transform transaction";
        return false;
    }''')
replace_once('src/editor/src/editor_session.cpp',
'''bool EditorSession::writeAutosave(std::string* error) const {
    if (!requireProject(error)) return false;''',
'''bool EditorSession::writeAutosave(std::string* error) const {
    if (!requireProject(error)) return false;
    if (transformTransaction_) {
        if (error) *error = "Cannot autosave during an active transform transaction";
        return false;
    }''')
replace_once('src/editor/src/editor_session.cpp',
'''const ProjectDocument* EditorSession::document() const noexcept { return document_ ? &*document_ : nullptr; }''',
'''bool EditorSession::isDirty() const noexcept {
    return recoveredDirty_ || commands_.isDirty() || transformTransactionHasChanges();
}

const ProjectDocument* EditorSession::document() const noexcept { return document_ ? &*document_ : nullptr; }''')
# Block mutations
for sig in [
    'std::optional<ObjectId> EditorSession::createObject(ObjectType type, std::string name,\n                                                     std::optional<ObjectId> parent) {\n    if (!document_) return std::nullopt;',
    'std::optional<ObjectId> EditorSession::createMeshObject(MeshResource resource, std::string name,\n                                                         std::optional<ObjectId> parent) {\n    if (!document_) return std::nullopt;',
    'bool EditorSession::deleteObject(ObjectId object) {\n    if (!document_ || !document_->scene.contains(object)) return false;',
    'bool EditorSession::deleteSelection() {\n    if (!document_ || selection_.empty()) return false;',
    'bool EditorSession::renameObject(ObjectId object, std::string name) {\n    if (!document_ || !document_->scene.contains(object)) return false;',
    'bool EditorSession::transformObject(ObjectId object, const Transform& transform) {\n    if (!document_ || !document_->scene.contains(object)) return false;',
    'bool EditorSession::reparentObject(ObjectId object, std::optional<ObjectId> parent) {\n    if (!document_ || !document_->scene.contains(object)) return false;',
    'bool EditorSession::undo() {\n    if (!document_ || !commands_.undo()) return false;',
    'bool EditorSession::redo() {\n    if (!document_ || !commands_.redo()) return false;',
]:
    first, rest = sig.split('\n',1)
    if sig not in Path('src/editor/src/editor_session.cpp').read_text(): raise SystemExit('mutation anchor missing: '+first)
    rep = first+'\n    if (transformTransaction_) return '+('std::nullopt;' if first.startswith('std::optional') else 'false;')+'\n'+rest
    replace_once('src/editor/src/editor_session.cpp', sig, rep)

# Add transaction methods before reparent
replace_once('src/editor/src/editor_session.cpp',
'''bool EditorSession::reparentObject(ObjectId object, std::optional<ObjectId> parent) {''',
'''bool EditorSession::beginTransformTransaction(const std::vector<ObjectId>& objects,
                                              std::string commandName) {
    if (!document_ || transformTransaction_ || objects.empty()) return false;
    TransformTransactionState transaction;
    transaction.commandName = commandName.empty() ? "Transform Objects" : std::move(commandName);
    transaction.changes.reserve(objects.size());
    for (const auto objectId : objects) {
        const auto* object = document_->scene.find(objectId);
        if (!object) return false;
        const auto duplicate = std::find_if(transaction.changes.cbegin(), transaction.changes.cend(),
                                            [objectId](const TransformChange& change) {
                                                return change.object == objectId;
                                            });
        if (duplicate != transaction.changes.cend()) return false;
        transaction.changes.push_back(TransformChange{
            .object = objectId,
            .before = object->localTransform,
            .after = object->localTransform,
        });
    }
    transformTransaction_ = std::move(transaction);
    return true;
}

bool EditorSession::previewTransform(ObjectId object, const Transform& transform) {
    if (!document_ || !transformTransaction_) return false;
    auto found = std::find_if(transformTransaction_->changes.begin(), transformTransaction_->changes.end(),
                              [object](const TransformChange& change) {
                                  return change.object == object;
                              });
    if (found == transformTransaction_->changes.end()) return false;
    if (!document_->scene.setTransform(object, transform)) return false;
    found->after = transform;
    ++sceneRevision_;
    return true;
}

bool EditorSession::commitTransformTransaction() {
    if (!document_ || !transformTransaction_) return false;
    std::vector<TransformChange> changes;
    changes.reserve(transformTransaction_->changes.size());
    for (const auto& change : transformTransaction_->changes) {
        if (change.before != change.after) changes.push_back(change);
    }
    const std::string commandName = transformTransaction_->commandName;
    if (changes.empty()) {
        transformTransaction_.reset();
        return true;
    }
    auto command = std::make_unique<TransformObjectsCommand>(document_->scene, changes, commandName);
    if (!commands_.execute(std::move(command))) {
        for (const auto& change : changes) {
            (void)document_->scene.setTransform(change.object, change.before);
        }
        transformTransaction_.reset();
        ++sceneRevision_;
        return false;
    }
    transformTransaction_.reset();
    sceneMutated(false);
    return true;
}

bool EditorSession::cancelTransformTransaction() {
    if (!document_ || !transformTransaction_) return false;
    bool success = true;
    for (const auto& change : transformTransaction_->changes) {
        success = document_->scene.setTransform(change.object, change.before) && success;
    }
    transformTransaction_.reset();
    ++sceneRevision_;
    return success;
}

bool EditorSession::reparentObject(ObjectId object, std::optional<ObjectId> parent) {''')
# selection lock
replace_once('src/editor/src/editor_session.cpp',
'''bool EditorSession::select(ObjectId object, SelectionMode mode) {
    if (!document_ || !selection_.select(document_->scene, object, mode)) return false;''',
'''bool EditorSession::select(ObjectId object, SelectionMode mode) {
    if (transformTransaction_) return false;
    if (!document_ || !selection_.select(document_->scene, object, mode)) return false;''')
replace_once('src/editor/src/editor_session.cpp',
'''void EditorSession::clearSelection() noexcept {
    if (selection_.empty()) return;''',
'''void EditorSession::clearSelection() noexcept {
    if (transformTransaction_ || selection_.empty()) return;''')
# reset and helper
replace_once('src/editor/src/editor_session.cpp',
'''void EditorSession::resetForDocument(bool recoveredDirty) noexcept {
    commands_.clear();''',
'''bool EditorSession::transformTransactionHasChanges() const noexcept {
    if (!transformTransaction_) return false;
    return std::any_of(transformTransaction_->changes.cbegin(), transformTransaction_->changes.cend(),
                       [](const TransformChange& change) { return change.before != change.after; });
}

void EditorSession::resetForDocument(bool recoveredDirty) noexcept {
    transformTransaction_.reset();
    commands_.clear();''')
# include algorithm
replace_once('src/editor/src/editor_session.cpp', '#include <memory>\n', '#include <algorithm>\n#include <memory>\n')

# Tests
p=Path('tests/test_editor_session.cpp'); s=p.read_text()
s += r'''

TEST_CASE("transform transaction previews many objects but commits one undo step") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Transform Transaction", &error));
    const auto first = session.createObject(m3d::ObjectType::Empty, "First");
    const auto second = session.createObject(m3d::ObjectType::Empty, "Second");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(session.saveProject(&error));

    const auto firstBefore = session.scene()->find(*first)->localTransform;
    const auto secondBefore = session.scene()->find(*second)->localTransform;
    REQUIRE(session.beginTransformTransaction({*first, *second}, "Move Objects"));
    REQUIRE(session.hasTransformTransaction());
    REQUIRE(!session.isDirty());

    auto firstPreview = firstBefore;
    firstPreview.position = {1.0F, 2.0F, 3.0F};
    auto secondPreview = secondBefore;
    secondPreview.position = {-4.0F, 5.0F, 6.0F};
    REQUIRE(session.previewTransform(*first, firstPreview));
    REQUIRE(session.previewTransform(*second, secondPreview));
    REQUIRE(session.isDirty());
    REQUIRE(session.scene()->find(*first)->localTransform == firstPreview);
    REQUIRE(session.scene()->find(*second)->localTransform == secondPreview);
    REQUIRE(!session.saveProject(&error));
    REQUIRE(!session.writeAutosave(&error));

    REQUIRE(session.commitTransformTransaction());
    REQUIRE(!session.hasTransformTransaction());
    REQUIRE(session.nextUndoName() == "Move Objects");
    REQUIRE(session.undo());
    REQUIRE(session.scene()->find(*first)->localTransform == firstBefore);
    REQUIRE(session.scene()->find(*second)->localTransform == secondBefore);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->find(*first)->localTransform == firstPreview);
    REQUIRE(session.scene()->find(*second)->localTransform == secondPreview);
}

TEST_CASE("cancelled transform transaction restores preview and does not create history") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Cancel Transform", &error));
    const auto object = session.createObject(m3d::ObjectType::Empty, "Object");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    const auto previousUndo = session.nextUndoName();
    const auto before = session.scene()->find(*object)->localTransform;
    REQUIRE(session.beginTransformTransaction({*object}, "Move Object"));
    auto preview = before;
    preview.position.x = 12.0F;
    REQUIRE(session.previewTransform(*object, preview));
    REQUIRE(session.isDirty());
    REQUIRE(!session.select(*object, m3d::SelectionMode::Replace));
    REQUIRE(!session.undo());
    REQUIRE(session.cancelTransformTransaction());
    REQUIRE(session.scene()->find(*object)->localTransform == before);
    REQUIRE(session.nextUndoName() == previousUndo);
    REQUIRE(!session.isDirty());
}
'''
p.write_text(s)

print('Stage 4 transform transaction applied')
