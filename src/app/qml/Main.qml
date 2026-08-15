pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    required property var controller

    visible: true
    width: 1280
    height: 720
    minimumWidth: 360
    minimumHeight: 520
    color: "#0d0f13"
    title: root.controller.projectOpen ? root.controller.projectName + " — Mobile3D Studio" : "Mobile3D Studio"

    property bool compact: width < 900 || height > width
    property bool outlinerPaneVisible: true
    property bool inspectorPaneVisible: true

    header: Column {
        width: parent.width

        TopBar {
            width: parent.width
            controller: root.controller
            compact: root.compact
            onToggleOutliner: {
                if (root.compact)
                    outlinerDrawer.open()
                else
                    root.outlinerPaneVisible = !root.outlinerPaneVisible
            }
            onToggleInspector: {
                if (root.compact)
                    inspectorDrawer.open()
                else
                    root.inspectorPaneVisible = !root.inspectorPaneVisible
            }
        }

        WorkspaceBar {
            width: parent.width
            height: visible ? implicitHeight : 0
            visible: root.controller.projectOpen
            controller: root.controller
        }
    }

    ProjectHome {
        anchors.fill: parent
        visible: !root.controller.projectOpen
        controller: root.controller
    }

    Item {
        anchors.fill: parent
        visible: root.controller.projectOpen

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                SplitView {
                    anchors.fill: parent
                    visible: !root.compact
                    orientation: Qt.Horizontal

                    handle: Rectangle {
                        implicitWidth: 12
                        color: SplitHandle.pressed ? "#315b87" : "#1b1f26"
                        Rectangle {
                            anchors.centerIn: parent
                            width: 2
                            height: Math.min(42, parent.height - 8)
                            radius: 1
                            color: "#343a45"
                        }
                    }

                    OutlinerPanel {
                        controller: root.controller
                        visible: root.outlinerPaneVisible
                        SplitView.preferredWidth: 270
                        SplitView.minimumWidth: 210
                        SplitView.maximumWidth: 420
                    }

                    ViewportPlaceholder {
                        controller: root.controller
                        SplitView.fillWidth: true
                        SplitView.minimumWidth: 320
                    }

                    InspectorPanel {
                        controller: root.controller
                        visible: root.inspectorPaneVisible
                        SplitView.preferredWidth: 310
                        SplitView.minimumWidth: 250
                        SplitView.maximumWidth: 440
                    }
                }

                Item {
                    anchors.fill: parent
                    visible: root.compact

                    ViewportPlaceholder {
                        anchors.fill: parent
                        controller: root.controller
                    }

                    Row {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        spacing: 8

                        Button {
                            width: (parent.width - parent.spacing) / 2
                            height: 48
                            text: "Scene"
                            onClicked: outlinerDrawer.open()
                        }
                        Button {
                            width: (parent.width - parent.spacing) / 2
                            height: 48
                            text: "Inspector"
                            onClicked: inspectorDrawer.open()
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: "#11141a"
                border.color: "#20242b"
                Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 20
                    text: root.controller.statusMessage.length > 0 ? root.controller.statusMessage : root.controller.projectPath
                    color: "#7f8794"
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                }
            }
        }
    }

    Drawer {
        id: outlinerDrawer
        edge: Qt.LeftEdge
        width: Math.min(root.width * 0.86, 360)
        height: root.height
        enabled: root.compact && root.controller.projectOpen
        modal: true

        OutlinerPanel {
            anchors.fill: parent
            controller: root.controller
        }
    }

    Drawer {
        id: inspectorDrawer
        edge: Qt.RightEdge
        width: Math.min(root.width * 0.90, 390)
        height: root.height
        enabled: root.compact && root.controller.projectOpen
        modal: true

        InspectorPanel {
            anchors.fill: parent
            controller: root.controller
        }
    }

    Dialog {
        id: recoveryDialog
        visible: root.controller.recoveryAvailable
        modal: true
        closePolicy: Popup.NoAutoClose
        title: "Recovery autosave found"
        width: Math.min(root.width - 32, 440)
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)

        contentItem: ColumnLayout {
            spacing: 14
            Label {
                Layout.fillWidth: true
                text: "A recovery scene exists for this project. Recover it before continuing, or discard it and keep the last primary save."
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    Layout.fillWidth: true
                    text: "Discard"
                    onClicked: root.controller.discardAutosave()
                }
                Button {
                    Layout.fillWidth: true
                    text: "Recover"
                    onClicked: root.controller.recoverAutosave()
                }
            }
        }
    }
}
