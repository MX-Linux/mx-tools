import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control

    // Follow the window's active state, as Main.qml's palette does, so these defaults
    // match the colors Main.qml derives from it.
    SystemPalette {
        id: systemPalette
        colorGroup: control.Window.active ? SystemPalette.Active : SystemPalette.Inactive
    }

    required property string toolName
    required property string description
    required property string categoryName
    required property string iconSource
    required property bool pinned
    signal pinRequested(bool pin)
    property bool condensed: false
    property color surfaceColor: systemPalette.base
    property color hoverSurfaceColor: Qt.tint(systemPalette.base, Qt.alpha(systemPalette.highlight, 0.08))
    property color primaryTextColor: systemPalette.text
    property color secondaryTextColor: Qt.alpha(systemPalette.text, 0.82)
    property color accentColor: systemPalette.highlight
    property color borderColor: Qt.alpha(systemPalette.text, 0.18)

    hoverEnabled: true
    padding: condensed ? 12 : 18
    Accessible.name: toolName
    Accessible.description: description

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_P && event.modifiers === Qt.NoModifier) {
            control.pinRequested(!control.pinned)
            event.accepted = true
        }
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    scale: down ? 0.985 : 1
    Behavior on scale { NumberAnimation { duration: 90 } }

    background: Rectangle {
        color: control.hovered ? control.hoverSurfaceColor : control.surfaceColor
        radius: 6
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus || control.hovered ? control.accentColor : control.borderColor

        Behavior on color { ColorAnimation { duration: 130 } }
        Behavior on border.color { ColorAnimation { duration: 130 } }
    }

    contentItem: RowLayout {
        spacing: control.condensed ? 10 : 15

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: control.condensed ? 42 : 54
            Layout.preferredHeight: control.condensed ? 42 : 54
            radius: 6
            color: Qt.alpha(control.accentColor, control.hovered ? 0.16 : 0.10)

            Image {
                anchors.centerIn: parent
                width: control.condensed ? 30 : 38
                height: control.condensed ? 30 : 38
                source: control.iconSource
                sourceSize: Qt.size(48, 48)
                fillMode: Image.PreserveAspectFit
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: control.condensed ? 2 : 5

            Text {
                Layout.fillWidth: true
                Layout.rightMargin: 18
                text: control.toolName
                color: control.primaryTextColor
                font.pixelSize: control.font.pixelSize + (control.condensed ? 1 : 3)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                id: descriptionText

                Layout.fillWidth: true
                Layout.fillHeight: true
                text: control.description.length > 0 ? control.description : qsTr("Open this MX tool")
                color: control.secondaryTextColor
                font.pixelSize: control.font.pixelSize
                lineHeight: 1.15
                wrapMode: Text.Wrap
                maximumLineCount: control.condensed ? 2 : 3
                elide: Text.ElideRight
            }

            Text {
                text: control.categoryName.toUpperCase()
                color: control.accentColor
                font.pixelSize: Math.max(9, control.font.pixelSize - 3)
                font.weight: Font.DemiBold
                font.letterSpacing: 0.7
            }
        }
    }

    // Overlay the fold so the pin control consumes no column in the card layout.
    ToolButton {
        id: pinButton
        objectName: "pinButton"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 1
        width: 32
        height: 32
        z: 1
        padding: 0
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        Accessible.name: control.pinned ? qsTr("Unpin %1").arg(control.toolName) : qsTr("Pin %1").arg(control.toolName)
        onClicked: control.pinRequested(!control.pinned)

        background: Canvas {
            property color foldColor: Qt.alpha(control.pinned || pinButton.hovered ? control.accentColor : control.primaryTextColor,
                                               pinButton.hovered ? 0.28 : control.pinned ? 0.20 : 0.07)
            property color edgeColor: control.pinned || pinButton.hovered ? control.accentColor : control.borderColor
            onFoldColorChanged: requestPaint()
            onEdgeColorChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(width - 5, 0)
                ctx.quadraticCurveTo(width, 0, width, 5)
                ctx.lineTo(width, height)
                ctx.closePath()
                ctx.fillStyle = foldColor
                ctx.fill()
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(width, height)
                ctx.strokeStyle = edgeColor
                ctx.lineWidth = 1
                ctx.stroke()
            }
        }
        contentItem: Item {
            Text {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 2
                anchors.rightMargin: 3
                text: control.pinned ? "★" : "☆"
                font.pixelSize: 15
                color: control.pinned || pinButton.hovered ? control.accentColor : control.secondaryTextColor
            }
        }
        ThemeToolTip {
            visible: pinButton.hovered
            text: control.pinned ? qsTr("Unpin tool") : qsTr("Pin tool")
            delay: 600
        }
    }

    ThemeToolTip {
        visible: control.hovered && !pinButton.hovered && descriptionText.truncated
        text: descriptionText.text
        delay: 600
    }
}
