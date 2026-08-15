#pragma once

#include "mobile3d/editor/editor_session.hpp"
#include "mobile3d/render/render_snapshot.hpp"

#include <QAbstractItemModel>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTimer>

#include <memory>

class OutlinerModel;

class EditorController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool projectOpen READ projectOpen NOTIFY projectStateChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectStateChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectStateChanged)
    Q_PROPERTY(int sceneObjectCount READ sceneObjectCount NOTIFY projectStateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY historyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY historyChanged)
    Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY historyChanged)
    Q_PROPERTY(QString workspace READ workspace NOTIFY workspaceChanged)
    Q_PROPERTY(QStringList workspaceNames READ workspaceNames CONSTANT)
    Q_PROPERTY(QAbstractItemModel* outlinerModel READ outlinerModel CONSTANT)
    Q_PROPERTY(bool hasActiveObject READ hasActiveObject NOTIFY selectionChanged)
    Q_PROPERTY(QString activeObjectName READ activeObjectName NOTIFY selectionChanged)
    Q_PROPERTY(QString activeObjectType READ activeObjectType NOTIFY selectionChanged)
    Q_PROPERTY(double positionX READ positionX NOTIFY selectionChanged)
    Q_PROPERTY(double positionY READ positionY NOTIFY selectionChanged)
    Q_PROPERTY(double positionZ READ positionZ NOTIFY selectionChanged)
    Q_PROPERTY(double scaleX READ scaleX NOTIFY selectionChanged)
    Q_PROPERTY(double scaleY READ scaleY NOTIFY selectionChanged)
    Q_PROPERTY(double scaleZ READ scaleZ NOTIFY selectionChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QStringList recentProjects READ recentProjects NOTIFY recentProjectsChanged)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY recoveryAvailableChanged)

public:
    explicit EditorController(QObject* parent = nullptr);
    ~EditorController() override;

    [[nodiscard]] bool projectOpen() const noexcept { return session_.hasProject(); }
    [[nodiscard]] QString projectName() const;
    [[nodiscard]] QString projectPath() const;
    [[nodiscard]] int sceneObjectCount() const;
    [[nodiscard]] bool dirty() const noexcept { return session_.isDirty(); }
    [[nodiscard]] bool canUndo() const noexcept { return session_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return session_.canRedo(); }
    [[nodiscard]] QString undoLabel() const;
    [[nodiscard]] QString redoLabel() const;
    [[nodiscard]] QString workspace() const;
    [[nodiscard]] QStringList workspaceNames() const;
    [[nodiscard]] QAbstractItemModel* outlinerModel() const noexcept;

    [[nodiscard]] bool hasActiveObject() const;
    [[nodiscard]] QString activeObjectName() const;
    [[nodiscard]] QString activeObjectType() const;
    [[nodiscard]] double positionX() const;
    [[nodiscard]] double positionY() const;
    [[nodiscard]] double positionZ() const;
    [[nodiscard]] double scaleX() const;
    [[nodiscard]] double scaleY() const;
    [[nodiscard]] double scaleZ() const;

    [[nodiscard]] QString statusMessage() const { return statusMessage_; }
    [[nodiscard]] QStringList recentProjects() const { return recentProjects_; }
    [[nodiscard]] bool recoveryAvailable() const noexcept { return recoveryAvailable_; }

    // Render-thread synchronization calls this only while the GUI thread is blocked by
    // Qt Quick's synchronization phase. The returned value owns a complete copy and
    // contains no Scene, SelectionModel, QObject, or GPU pointers.
    [[nodiscard]] m3d::RenderSceneSnapshot renderSnapshot() const {
        const auto* scene = session_.scene();
        if (!scene) {
            return {};
        }
        return m3d::RenderSnapshotBuilder::build(*scene, session_.selection(),
                                                 session_.sceneRevision(),
                                                 session_.selectionRevision());
    }

    Q_INVOKABLE bool createProject(const QString& name);
    Q_INVOKABLE bool openProject(const QString& path);
    Q_INVOKABLE bool openRecent(int index);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool autosaveNow();
    Q_INVOKABLE bool recoverAutosave();
    Q_INVOKABLE bool discardAutosave();
    Q_INVOKABLE void closeProject();

    Q_INVOKABLE bool addObject(const QString& typeName);
    Q_INVOKABLE bool deleteSelection();
    Q_INVOKABLE bool selectObject(const QString& objectId, bool toggle = false);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool renameActive(const QString& name);
    Q_INVOKABLE bool setActivePosition(double x, double y, double z);
    Q_INVOKABLE bool setActiveScale(double x, double y, double z);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool setWorkspace(const QString& name);

public slots:
    void handleApplicationState(Qt::ApplicationState state);

signals:
    void projectStateChanged();
    void historyChanged();
    void selectionChanged();
    void workspaceChanged();
    void statusMessageChanged();
    void recentProjectsChanged();
    void recoveryAvailableChanged();

private:
    [[nodiscard]] const m3d::SceneObject* activeObject() const;
    [[nodiscard]] static QString objectTypeName(m3d::ObjectType type);
    [[nodiscard]] static std::optional<m3d::ObjectType> objectTypeFromName(const QString& name);
    [[nodiscard]] static std::optional<m3d::Workspace> workspaceFromName(const QString& name);
    [[nodiscard]] static std::filesystem::path toFilesystemPath(const QString& value);
    [[nodiscard]] static QString fromFilesystemPath(const std::filesystem::path& value);

    void refreshUi();
    void setStatus(QString message);
    void setRecoveryAvailable(bool value);
    void loadRecentProjects();
    void rememberProject(const QString& path);
    [[nodiscard]] QString allocateProjectPath(const QString& name) const;

    m3d::EditorSession session_;
    std::unique_ptr<OutlinerModel> outliner_;
    QSettings settings_;
    QTimer autosaveTimer_;
    QStringList recentProjects_;
    QString statusMessage_;
    bool recoveryAvailable_{false};
};
