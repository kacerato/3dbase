import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var controller

    color: "#14171c"
    border.color: "#252a33"

    function numeric(text, fallback) {
        var value = Number(text)
        return Number.isFinite(value) ? value : fallback
    }

    function refreshFields() {
        if (!controller.hasActiveObject)
            return
        nameField.text = controller.activeObjectName
        posX.text = Number(controller.positionX).toFixed(3)
        posY.text = Number(controller.positionY).toFixed(3)
        posZ.text = Number(controller.positionZ).toFixed(3)
        scaleX.text = Number(controller.scaleX).toFixed(3)
        scaleY.text = Number(controller.scaleY).toFixed(3)
        scaleZ.text = Number(controller.scaleZ).toFixed(3)
    }

    Component.onCompleted: refreshFields()

    Connections {
        target: root.controller
        function onSelectionChanged() { root.refreshFields() }
    }

    DoubleValidator {
        id: numberValidator
        bottom: -1000000
        top: 1000000
        decimals: 6
        notation: DoubleValidator.StandardNotation
    }

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
                text: "Inspector"
                color: "#e5e7eb"
                font.weight: Font.DemiBold
            }
            Label {
                visible: root.controller.hasActiveObject
                text: root.controller.activeObjectType
                color: "#8b93a1"
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#252a33" }

        Label {
            visible: !root.controller.hasActiveObject
            Layout.fillWidth: true
            Layout.margins: 16
            text: "Select an object to inspect it."
            color: "#7f8794"
            wrapMode: Text.WordWrap
        }

        ScrollView {
            id: inspectorScroll
            visible: root.controller.hasActiveObject
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth

            ColumnLayout {
                width: inspectorScroll.availableWidth
                spacing: 10

                Label { text: "Object"; color: "#9da5b2"; Layout.leftMargin: 12; Layout.topMargin: 12 }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    placeholderText: "Name"
                    onEditingFinished: root.controller.renameActive(text)
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#252a33"; Layout.topMargin: 4 }
                Label { text: "Position"; color: "#9da5b2"; Layout.leftMargin: 12 }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    TextField { id: posX; Layout.fillWidth: true; placeholderText: "X"; validator: numberValidator; inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.controller.setActivePosition(root.numeric(posX.text, root.controller.positionX), root.numeric(posY.text, root.controller.positionY), root.numeric(posZ.text, root.controller.positionZ)) }
                    TextField { id: posY; Layout.fillWidth: true; placeholderText: "Y"; validator: numberValidator; inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.controller.setActivePosition(root.numeric(posX.text, root.controller.positionX), root.numeric(posY.text, root.controller.positionY), root.numeric(posZ.text, root.controller.positionZ)) }
                    TextField { id: posZ; Layout.fillWidth: true; placeholderText: "Z"; validator: numberValidator; inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.controller.setActivePosition(root.numeric(posX.text, root.controller.positionX), root.numeric(posY.text, root.controller.positionY), root.numeric(posZ.text, root.controller.positionZ)) }
                }

                Label { text: "Scale"; color: "#9da5b2"; Layout.leftMargin: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    TextField { id: scaleX; Layout.fillWidth: true; placeholderText: "X"; validator: numberValidator; inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.controller.setActiveScale(root.numeric(scaleX.text, root.controller.scaleX), root.numeric(scaleY.text, root.controller.scaleY), root.numeric(scaleZ.text, root.controller.scaleZ)) }
                    TextField { id: scaleY; Layout.fillWidth: true; placeholderText: "Y"; validator: numberValidator; inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.controller.setActiveScale(root.numeric(scaleX.text, root.controller.scaleX), root.numeric(scaleY.text, root.controller.scaleY), root.numeric(scaleZ.text, root.controller.scaleZ)) }
                    TextField { id: scaleZ; Layout.fillWidth: true; placeholderText: "Z"; validator: numberValidator; inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.controller.setActiveScale(root.numeric(scaleX.text, root.controller.scaleX), root.numeric(scaleY.text, root.controller.scaleY), root.numeric(scaleZ.text, root.controller.scaleZ)) }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    text: "Rotation is preserved in the core as a quaternion. Interactive rotation editing is intentionally deferred to the transform/gizmo stage instead of introducing a temporary Euler path."
                    color: "#6f7784"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                }

                Button {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 8
                    text: "Delete selected"
                    onClicked: root.controller.deleteSelection()
                }

                Item { Layout.preferredHeight: 18 }
            }
        }
    }
}
