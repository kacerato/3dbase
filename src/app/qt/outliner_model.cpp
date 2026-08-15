#include "outliner_model.hpp"

#include <QVariant>

#include <algorithm>

OutlinerModel::OutlinerModel(m3d::EditorSession& session, QObject* parent)
    : QAbstractListModel(parent), session_(session) {}

int OutlinerModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant OutlinerModel::data(const QModelIndex& index, int role) const {
    const int rowIndex = index.row();
    if (!index.isValid() || rowIndex < 0 || rowIndex >= static_cast<int>(rows_.size())) {
        return {};
    }

    const auto* scene = session_.scene();
    if (!scene) {
        return {};
    }

    const auto& row = rows_.at(rowIndex);
    const auto* object = scene->find(row.id);
    if (!object) {
        return {};
    }

    switch (role) {
    case ObjectIdRole:
        return QString::fromStdString(row.id.toString());
    case DisplayNameRole:
        return QString::fromStdString(object->name);
    case TypeNameRole:
        return typeName(object->type);
    case DepthRole:
        return row.depth;
    case SelectedRole:
        return session_.selection().contains(row.id);
    case ActiveRole: {
        const auto active = session_.selection().active();
        return active && *active == row.id;
    }
    case HasChildrenRole:
        return row.hasChildren;
    default:
        return {};
    }
}

QHash<int, QByteArray> OutlinerModel::roleNames() const {
    return {
        {ObjectIdRole, "objectId"},
        {DisplayNameRole, "displayName"},
        {TypeNameRole, "typeName"},
        {DepthRole, "depth"},
        {SelectedRole, "selected"},
        {ActiveRole, "active"},
        {HasChildrenRole, "hasChildren"},
    };
}

void OutlinerModel::refresh() {
    beginResetModel();
    rows_.clear();

    if (const auto* scene = session_.scene()) {
        auto roots = scene->roots();
        sortIds(*scene, roots);
        for (const auto root : roots) {
            appendSubtree(*scene, root, 0);
        }
    }

    endResetModel();
}

void OutlinerModel::appendSubtree(const m3d::Scene& scene, m3d::ObjectId id, int depth) {
    auto children = scene.childrenOf(id);
    sortIds(scene, children);
    rows_.push_back(Row{id, depth, !children.empty()});
    for (const auto child : children) {
        appendSubtree(scene, child, depth + 1);
    }
}

void OutlinerModel::sortIds(const m3d::Scene& scene, std::vector<m3d::ObjectId>& ids) {
    std::sort(ids.begin(), ids.end(), [&scene](m3d::ObjectId lhs, m3d::ObjectId rhs) {
        const auto* left = scene.find(lhs);
        const auto* right = scene.find(rhs);
        if (left && right && left->name != right->name) {
            return left->name < right->name;
        }
        return lhs.toString() < rhs.toString();
    });
}

QString OutlinerModel::typeName(m3d::ObjectType type) {
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
