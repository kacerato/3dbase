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
        objectName: "nativeVulkanViewport"
        anchors.fill: root
        controller: root.controller
    }

    // Minimal center guides stay subtle and never carry interaction state.
    Rectangle {
        anchors.centerIn: parent
        width: 1
        height: 12
        color: "#40505f68"
    }
    Rectangle {
        anchors.centerIn: parent
        width: 12
        height: 1
        color: "#40505f68"
    }

    Rectangle {
        visible: !nativeViewport.vulkanActive
        anchors.centerIn: parent
        width: fallbackLabel.implicitWidth + 28
        height: fallbackLabel.implicitHeight + 20
        radius: 8
        color: "#d90d1015"
        border.color: "#303641"

        Label {
            id: fallbackLabel
            anchors.centerIn: parent
            text: "Viewport fallback • " + nativeViewport.backendName
            color: "#aab2bf"
        }
    }

    // Transform state belongs to EditorController; these controls are only a
    // touch-friendly presentation of the same C++ state used by the viewport.
    Flow {
        id: transformToolbar
        visible: !root.controller.editMode
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        width: Math.min(620, parent.width - 20)
        spacing: 5

        Button {
            height: 36
            text: "Move"
            highlighted: root.controller.transformTool === "Move"
            enabled: !root.controller.transformInProgress
            onClicked: root.controller.setTransformTool("Move")
        }
        Button {
            height: 36
            text: "Rotate"
            highlighted: root.controller.transformTool === "Rotate"
            enabled: !root.controller.transformInProgress
            onClicked: root.controller.setTransformTool("Rotate")
        }
        Button {
            height: 36
            text: "Scale"
            highlighted: root.controller.transformTool === "Scale"
            enabled: !root.controller.transformInProgress
            onClicked: root.controller.setTransformTool("Scale")
        }
        Button {
            height: 36
            text: root.controller.transformSpace
            enabled: !root.controller.transformInProgress
            onClicked: root.controller.setTransformSpace(
                           root.controller.transformSpace === "Global" ? "Local" : "Global")
        }
        Button {
            height: 36
            text: "Pivot: " + root.controller.pivotMode
            enabled: !root.controller.transformInProgress
            onClicked: {
                if (root.controller.pivotMode === "Median")
                    root.controller.setPivotMode("Active")
                else if (root.controller.pivotMode === "Active")
                    root.controller.setPivotMode("Individual")
                else
                    root.controller.setPivotMode("Median")
            }
        }
        Button {
            height: 36
            text: "Layer: " + root.controller.activeLayerName
            enabled: !root.controller.transformInProgress
            onClicked: {
                const names = root.controller.layerNames
                const current = Math.max(0, names.indexOf(root.controller.activeLayerName))
                root.controller.setActiveLayer(names[(current + 1) % names.length])
            }
        }
        Button {
            height: 36
            text: root.controller.transformSnapEnabled ? "Snap On" : "Snap Off"
            highlighted: root.controller.transformSnapEnabled
            enabled: !root.controller.transformInProgress
            onClicked: root.controller.setTransformSnapEnabled(
                           !root.controller.transformSnapEnabled)
        }
    }

    Flow {
        id: modelingToolbar
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        width: Math.min(760, parent.width - 20)
        spacing: 5
        visible: root.controller.editMode || root.controller.workspace === "Modeling"

        Button {
            height: 38
            text: root.controller.editMode ? "Object Mode" : "Edit Mode"
            highlighted: root.controller.editMode
            enabled: !root.controller.transformInProgress && root.controller.hasActiveObject
            onClicked: root.controller.toggleEditMode()
        }
        Repeater {
            model: root.controller.meshSelectionModes
            delegate: Button {
                required property string modelData
                height: 38
                visible: root.controller.editMode
                text: modelData
                highlighted: root.controller.meshSelectionMode === modelData
                onClicked: root.controller.setMeshSelectionMode(modelData)
            }
        }
        Button {
            height: 38
            visible: root.controller.editMode
            text: "Extrude"
            enabled: root.controller.meshSelectionMode === "Face"
                     && root.controller.selectedMeshElementCount === 1
            onClicked: root.controller.extrudeSelectedFace(0.25)
        }
        Button {
            height: 38
            visible: root.controller.editMode
            text: "Inset"
            enabled: root.controller.meshSelectionMode === "Face"
                     && root.controller.selectedMeshElementCount === 1
            onClicked: root.controller.insetSelectedFace(0.25)
        }
        Button {
            height: 38
            visible: root.controller.editMode
            text: "Subdivide"
            enabled: root.controller.meshSelectionMode === "Face"
                     && root.controller.selectedMeshElementCount === 1
            onClicked: root.controller.subdivideSelectedFace()
        }
        Button {
            height: 38
            visible: root.controller.editMode
            text: "Merge Active"
            enabled: root.controller.meshSelectionMode === "Vertex"
                     && root.controller.selectedMeshElementCount >= 2
            onClicked: root.controller.mergeSelectedVertices()
        }
        Button {
            height: 38
            visible: root.controller.editMode
            text: "Weld"
            enabled: root.controller.meshSelectionMode === "Vertex"
                     && root.controller.selectedMeshElementCount >= 2
            onClicked: root.controller.weldSelectedVertices(0.01)
        }
        Button {
            height: 38
            visible: root.controller.editMode
            text: "Loop Cut"
            enabled: root.controller.meshSelectionMode === "Edge"
                     && root.controller.selectedMeshElementCount === 1
            onClicked: root.controller.loopCutSelectedEdge()
        }
        Button {
            height: 38
            visible: root.controller.editMode
            text: "Fill"
            enabled: root.controller.meshSelectionMode === "Edge"
                     && root.controller.selectedMeshElementCount >= 3
            onClicked: root.controller.fillSelectedBoundary()
        }
        Button {
            height: 38
            visible: root.controller.editMode
            text: "Bridge"
            enabled: root.controller.meshSelectionMode === "Edge"
                     && root.controller.selectedMeshElementCount >= 6
            onClicked: root.controller.bridgeSelectedBoundaries()
        }
        Button {
            height: 38
            visible: root.controller.editMode
            text: "Cancel"
            onClicked: root.controller.cancelEditMode()
        }
    }

    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        spacing: 6

        Button {
            height: 36
            text: nativeViewport.projectionName
            enabled: !root.controller.transformInProgress
            onClicked: nativeViewport.toggleProjection()
        }
        Button {
            height: 36
            text: "Reset view"
            enabled: !root.controller.transformInProgress
            onClicked: nativeViewport.resetCamera()
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
            text: nativeViewport.backendName
                  + (nativeViewport.vulkanActive ? " • native" : " • fallback")
                  + (root.controller.transformInProgress ? " • transforming" : "")
                  + (root.controller.editMode ? " • Edit " + root.controller.meshSelectionMode : "")
            color: "#9aa3b1"
            font.pixelSize: 11
        }
    }
}
