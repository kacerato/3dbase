pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var controller
    property bool compact: false

    signal toggleOutliner()
    signal toggleInspector()

    implicitHeight: 56
    color: "#15181e"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 6

        ToolButton {
            text: "Tree"
            Layout.minimumWidth: 52
            onClicked: root.toggleOutliner()
        }

        Label {
            Layout.fillWidth: true
            text: root.controller.projectOpen
                  ? root.controller.projectName + (root.controller.dirty ? " *" : "")
                  : "Mobile3D Studio"
            color: "#f4f6f8"
            elide: Text.ElideRight
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }

        ToolButton {
            visible: root.controller.projectOpen
            enabled: root.controller.canUndo
            text: root.compact ? "Undo" : root.controller.undoLabel
            onClicked: root.controller.undo()
        }

        ToolButton {
            visible: root.controller.projectOpen
            enabled: root.controller.canRedo
            text: root.compact ? "Redo" : root.controller.redoLabel
            onClicked: root.controller.redo()
        }

        ToolButton {
            id: addButton
            visible: root.controller.projectOpen
            text: "Add"
            onClicked: addMenu.open()

            Menu {
                id: addMenu
                y: addButton.height
                MenuItem { text: "Mesh object"; onTriggered: root.controller.addObject("Mesh") }
                MenuItem { text: "Empty"; onTriggered: root.controller.addObject("Empty") }
                MenuItem { text: "Camera"; onTriggered: root.controller.addObject("Camera") }
                MenuItem { text: "Light"; onTriggered: root.controller.addObject("Light") }
                MenuItem { text: "Curve"; onTriggered: root.controller.addObject("Curve") }
            }
        }

        ToolButton {
            visible: root.controller.projectOpen
            text: "Save"
            onClicked: root.controller.saveProject()
        }

        ToolButton {
            text: "Info"
            Layout.minimumWidth: 52
            onClicked: root.toggleInspector()
        }

        ToolButton {
            visible: root.controller.projectOpen && !root.compact
            text: "Close"
            onClicked: root.controller.closeProject()
        }
    }
}
