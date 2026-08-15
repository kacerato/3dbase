import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var controller

    color: "#0d0f13"

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: Math.min(640, Math.max(320, root.width - 32))
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 14

            Item { Layout.preferredHeight: 28 }

            Label {
                text: "Mobile3D Studio"
                color: "#f7f8fa"
                font.pixelSize: 28
                font.weight: Font.Bold
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: "Native mobile 3D authoring foundation"
                color: "#9ca3af"
                Layout.alignment: Qt.AlignHCenter
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: createColumn.implicitHeight + 28
                radius: 10
                color: "#171a20"
                border.color: "#252a33"

                ColumnLayout {
                    id: createColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Label { text: "Create project"; color: "#e5e7eb"; font.weight: Font.DemiBold }
                    TextField {
                        id: projectName
                        Layout.fillWidth: true
                        placeholderText: "Project name"
                        maximumLength: 80
                        onAccepted: createButton.clicked()
                    }
                    Button {
                        id: createButton
                        Layout.fillWidth: true
                        text: "Create"
                        enabled: projectName.text.trim().length > 0
                        onClicked: if (root.controller.createProject(projectName.text)) projectName.clear()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: openColumn.implicitHeight + 28
                radius: 10
                color: "#171a20"
                border.color: "#252a33"

                ColumnLayout {
                    id: openColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Label { text: "Open existing project"; color: "#e5e7eb"; font.weight: Font.DemiBold }
                    TextField {
                        id: openPath
                        Layout.fillWidth: true
                        placeholderText: "Project folder path"
                        onAccepted: root.controller.openProject(openPath.text)
                    }
                    Button {
                        Layout.fillWidth: true
                        text: "Open path"
                        enabled: openPath.text.trim().length > 0
                        onClicked: root.controller.openProject(openPath.text)
                    }
                }
            }

            Label {
                visible: root.controller.recentProjects.length > 0
                text: "Recent projects"
                color: "#e5e7eb"
                font.weight: Font.DemiBold
            }

            Repeater {
                model: root.controller.recentProjects

                delegate: Button {
                    required property string modelData
                    required property int index

                    Layout.fillWidth: true
                    text: modelData
                    horizontalAlignment: Text.AlignLeft
                    onClicked: root.controller.openRecent(index)
                }
            }

            Label {
                Layout.fillWidth: true
                text: "New projects are stored in the app data directory. External import/export will use a dedicated asset pipeline later instead of mixing Android file access into the scene core."
                color: "#7f8794"
                wrapMode: Text.WordWrap
            }

            Item { Layout.preferredHeight: 32 }
        }
    }
}
