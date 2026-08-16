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

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: collectionBox
                    Layout.fillWidth: true
                    model: root.controller.collectionNames
                    displayText: currentText.length > 0 ? currentText : "Collections"
                }
                ToolButton { text: "+C"; onClicked: root.controller.createCollection() }
                ToolButton {
                    text: "Add"
                    enabled: collectionBox.currentText.length > 0
                    onClicked: root.controller.addSelectionToCollection(collectionBox.currentText)
                }
                ToolButton {
                    text: "◉"
                    enabled: collectionBox.currentText.length > 0
                    onClicked: root.controller.toggleCollectionVisible(collectionBox.currentText)
                }
                ToolButton {
                    text: "🔒"
                    enabled: collectionBox.currentText.length > 0
                    onClicked: root.controller.toggleCollectionLocked(collectionBox.currentText)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: layerBox
                    Layout.fillWidth: true
                    model: root.controller.layerNames
                    onActivated: root.controller.setActiveLayer(currentText)
                }
                ToolButton { text: "+L"; onClicked: root.controller.createLayer() }
                ToolButton {
                    text: "Link"
                    enabled: collectionBox.currentText.length > 0 && layerBox.currentText !== "All"
                    onClicked: root.controller.addCollectionToLayer(collectionBox.currentText, layerBox.currentText)
                }
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
                required property bool objectVisible
                required property bool objectLocked

                width: ListView.view.width
                height: 48
                color: rowRoot.selected ? (rowRoot.active ? "#244b75" : "#20364f") : (mouseArea.pressed ? "#20242c" : "transparent")

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    z: 0
                    onClicked: root.controller.selectObject(rowRoot.objectId, false)
                    onPressAndHold: root.controller.selectObject(rowRoot.objectId, true)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10 + rowRoot.depth * 18
                    anchors.rightMargin: 8
                    spacing: 8
                    z: 1

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

                    ToolButton {
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        text: rowRoot.objectVisible ? "◉" : "○"
                        onClicked: root.controller.setObjectVisible(rowRoot.objectId, !rowRoot.objectVisible)
                    }

                    ToolButton {
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        text: rowRoot.objectLocked ? "🔒" : "🔓"
                        onClicked: root.controller.setObjectLocked(rowRoot.objectId, !rowRoot.objectLocked)
                    }
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
