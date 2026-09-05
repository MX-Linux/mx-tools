import QtQuick
import QtQuick.Controls

Button {
    id: control

    SystemPalette { id: systemPalette }

    property color textColor: systemPalette.buttonText
    property color surfaceColor: "transparent"
    property color hoverColor: Qt.alpha(systemPalette.highlight, 0.13)
    property color borderColor: Qt.alpha(systemPalette.text, 0.18)
    property color accentColor: systemPalette.highlight

    implicitHeight: 38
    hoverEnabled: true
    padding: 9
    leftPadding: 13
    rightPadding: 13
    Accessible.name: text

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: Text {
        text: control.text
        color: control.textColor
        font.pixelSize: control.font.pixelSize
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 6
        color: control.hovered ? control.hoverColor : control.surfaceColor
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.accentColor : control.borderColor

        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }
}
