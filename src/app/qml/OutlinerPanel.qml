pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var controller

    color: "#14171c"
    border.color: "#252a33"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 10
            Layout.rightMargin: 8

            Label {
                Layout.fillWidth: true
                text: "Scene"
                color: "#e5e7eb"
                font.weight: Font.DemiBold
            }

            Label {
                text: root.controller.sceneObjectCount.toString()
                color: "#8b93a1"
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#252a33" }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.controller.outlinerModel

            delegate: Rectangle {
                id: rowRoot

                required property string objectId
                required property string displayName
                required property string typeName
                required property int depth
                required property bool selected
                required property bool active
                required property bool hasChildren

                width: ListView.view.width
                height: 48
                color: rowRoot.selected ? (rowRoot.active ? "#244b75" : "#20364f") : (mouseArea.pressed ? "#20242c" : "transparent")

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10 + rowRoot.depth * 18
                    anchors.rightMargin: 8
                    spacing: 8

                    Label {
                        text: rowRoot.hasChildren ? "▸" : "·"
                        color: "#707887"
                        Layout.preferredWidth: 14
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            Layout.fillWidth: true
                            text: rowRoot.displayName
                            color: "#edf0f4"
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: rowRoot.typeName
                            color: "#7f8794"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    onClicked: root.controller.selectObject(rowRoot.objectId, false)
                    onPressAndHold: root.controller.selectObject(rowRoot.objectId, true)
                }
            }

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "Scene is empty\nUse Add to create an object"
                color: "#717986"
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
