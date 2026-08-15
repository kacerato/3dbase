import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property var controller

    implicitHeight: 42
    color: "#101319"

    ListView {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        orientation: ListView.Horizontal
        spacing: 4
        clip: true
        model: root.controller.workspaceNames

        delegate: Button {
            required property string modelData

            height: 36
            text: modelData
            checkable: true
            checked: root.controller.workspace === modelData
            onClicked: root.controller.setWorkspace(modelData)
        }
    }
}
