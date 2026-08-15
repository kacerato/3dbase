pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property var controller

    color: nativeViewport.vulkanActive ? "transparent" : "#0b0d11"
    border.color: "#20242b"
    clip: true

    VulkanViewport {
        id: nativeViewport
        anchors.fill: parent
        controller: root.controller
    }

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
            text: nativeViewport.vulkanActive ? "Vulkan viewport foundation" : "Viewport fallback"
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
            width: Math.min(460, root.width - 40)
            text: nativeViewport.vulkanActive
                  ? "Native Vulkan commands are recording inside this viewport using an immutable render snapshot. Mesh drawing, camera and picking are the next Stage 3 slices."
                  : "Graphics backend: " + nativeViewport.backendName + ". Android production builds request Vulkan; host smoke tests intentionally keep a software fallback."
            color: "#626b78"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: backendLabel.implicitWidth + 16
        height: 28
        radius: 6
        color: "#a611141a"
        border.color: "#303641"

        Label {
            id: backendLabel
            anchors.centerIn: parent
            text: nativeViewport.backendName + (nativeViewport.vulkanActive ? " • native underlay" : " • fallback")
            color: "#9aa3b1"
            font.pixelSize: 11
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.controller.clearSelection()
    }
}
