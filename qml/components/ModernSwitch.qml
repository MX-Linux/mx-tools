import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control

    SystemPalette { id: systemPalette }

    property color accentColor: systemPalette.highlight
    property color inactiveColor: systemPalette.mid
    property color knobColor: checked ? systemPalette.highlightedText : systemPalette.button
    property color knobBorderColor: Qt.alpha(systemPalette.shadow, 0.25)

    implicitWidth: 42
    implicitHeight: 28
    checkable: true
    hoverEnabled: true
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    background: Item {}

    contentItem: Item {
        Rectangle {
            id: track
            anchors.centerIn: parent
            width: 40
            height: 22
            radius: height / 2
            color: control.checked ? control.accentColor : control.inactiveColor
            border.width: control.activeFocus ? 2 : 0
            border.color: control.accentColor

            Behavior on color { ColorAnimation { duration: 130 } }

            Rectangle {
                width: 18
                height: 18
                radius: width / 2
                x: control.checked ? track.width - width - 2 : 2
                anchors.verticalCenter: parent.verticalCenter
                color: control.knobColor
                border.width: 1
                border.color: control.knobBorderColor
                scale: control.down ? 0.9 : 1

                Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                Behavior on scale { NumberAnimation { duration: 90 } }
            }
        }
    }
}
