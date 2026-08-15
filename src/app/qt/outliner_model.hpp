#pragma once

#include "mobile3d/editor/editor_session.hpp"

#include <QAbstractListModel>
#include <QVector>

class OutlinerModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        ObjectIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        TypeNameRole,
        DepthRole,
        SelectedRole,
        ActiveRole,
        HasChildrenRole,
    };

    explicit OutlinerModel(m3d::EditorSession& session, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void refresh();

private:
    struct Row final {
        m3d::ObjectId id{};
        int depth{0};
        bool hasChildren{false};
    };

    void appendSubtree(const m3d::Scene& scene, m3d::ObjectId id, int depth);
    static void sortIds(const m3d::Scene& scene, std::vector<m3d::ObjectId>& ids);
    [[nodiscard]] static QString typeName(m3d::ObjectType type);

    m3d::EditorSession& session_;
    QVector<Row> rows_;
};
