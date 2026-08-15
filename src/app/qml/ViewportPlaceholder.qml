import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property var controller

    color: "#0b0d11"
    border.color: "#20242b"
    clip: true

    Rectangle {
        anchors.centerIn: parent
        width: 1
        height: parent.height
        color: "#151922"
    }
    Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: 1
        color: "#151922"
    }

    Column {
        anchors.centerIn: parent
        spacing: 8
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Vulkan viewport — Stage 3"
            color: "#d6dae0"
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.controller.sceneObjectCount + " scene object" + (root.controller.sceneObjectCount === 1 ? "" : "s")
            color: "#747d8c"
        }
        Label {
            width: Math.min(440, root.width - 40)
            text: "The editor shell is connected to the real Scene now. This area stays intentionally non-rendering until the Vulkan lifecycle, swapchain, picking and camera systems are implemented together."
            color: "#626b78"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.controller.clearSelection()
    }
}
