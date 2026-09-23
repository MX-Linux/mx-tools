import QtQuick
import QtQuick.Layouts

// The "hide from the system menu" label and switch. Clicking the label toggles the
// switch too.
RowLayout {
    id: control

    required property var backend
    property bool wrapLabel: false
    property real fontPixelSize: Application.font.pixelSize
    readonly property string toolTipText: qsTranslate("Main", "They'll still be available here in MX Tools")

    spacing: 7

    SystemPalette {
        id: systemPalette
        colorGroup: control.Window.active ? SystemPalette.Active : SystemPalette.Inactive
    }

    Text {
        Layout.fillWidth: control.wrapLabel
        text: qsTranslate("Main", "Hide those tools from the system menu")
        color: Qt.alpha(systemPalette.text, 0.82)
        font.pixelSize: control.fontPixelSize
        wrapMode: control.wrapLabel ? Text.Wrap : Text.NoWrap
        Accessible.ignored: true

        HoverHandler {
            id: labelHover
            cursorShape: menuSwitch.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
        // Act exactly like a click on the switch.
        TapHandler {
            enabled: menuSwitch.enabled
            onTapped: {
                menuSwitch.toggle()
                menuSwitch.toggled()
            }
        }
        ThemeToolTip {
            visible: labelHover.hovered
            text: control.toolTipText
        }
    }

    ModernSwitch {
        id: menuSwitch
        checked: control.backend.hideFromMenu
        enabled: !control.backend.menuBusy
        Accessible.name: qsTranslate("Main", "Hide those tools from the system menu")
        ThemeToolTip {
            visible: menuSwitch.hovered
            text: control.toolTipText
        }
        onToggled: control.backend.hideFromMenu = checked
    }
}
