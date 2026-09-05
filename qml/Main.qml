import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "components"

ApplicationWindow {
    id: root

    SystemPalette {
        id: systemPalette
        colorGroup: root.active ? SystemPalette.Active : SystemPalette.Inactive
    }

    width: 1080
    height: 720
    minimumWidth: 720
    minimumHeight: 560
    visible: true
    title: qsTr("MX Tools")
    color: backgroundColor

    readonly property color backgroundColor: systemPalette.window
    readonly property color surfaceColor: systemPalette.base
    readonly property color raisedSurfaceColor: Qt.tint(systemPalette.base, Qt.alpha(systemPalette.highlight, 0.08))
    readonly property color primaryTextColor: systemPalette.text
    readonly property color secondaryTextColor: Qt.alpha(systemPalette.text, 0.68)
    readonly property color borderColor: Qt.alpha(systemPalette.text, 0.18)
    readonly property color accentColor: systemPalette.highlight
    readonly property color accentWash: Qt.alpha(systemPalette.highlight, 0.13)
    readonly property color inactiveControlColor: systemPalette.mid
    readonly property real baseFontSize: Application.font.pixelSize > 0 ? Application.font.pixelSize : 13
    readonly property bool compactNavigation: width < 900
    property bool condensedView: false
    required property var backend
    required property string version

    Settings {
        category: "MainWindow"
        property alias windowX: root.x
        property alias windowY: root.y
        property alias windowWidth: root.width
        property alias windowHeight: root.height
        property alias condensedView: root.condensedView
    }

    header: Rectangle {
        implicitHeight: 88
        color: root.surfaceColor
        border.color: root.borderColor
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 25
            anchors.rightMargin: 25
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                radius: 6
                color: root.accentWash

                Image {
                    anchors.centerIn: parent
                    width: 38
                    height: 38
                    source: "../icons/logo.svg"
                    sourceSize: Qt.size(48, 48)
                    fillMode: Image.PreserveAspectFit
                }
            }

            ColumnLayout {
                Layout.preferredWidth: root.compactNavigation ? 132 : 180
                spacing: 1

                Text {
                    text: qsTr("MX Tools")
                    color: root.primaryTextColor
                    font.pixelSize: root.baseFontSize + 7
                    font.weight: Font.Bold
                }
                Text {
                    visible: !root.compactNavigation
                    text: qsTr("System dashboard")
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(10, root.baseFontSize - 1)
                }
            }

            Item { Layout.fillWidth: true }

            TextField {
                id: searchField
                Layout.preferredWidth: Math.min(380, root.width * 0.36)
                Layout.minimumWidth: 210
                Layout.preferredHeight: 44
                leftPadding: 42
                rightPadding: 38
                placeholderText: ""
                color: root.primaryTextColor
                placeholderTextColor: root.secondaryTextColor
                selectByMouse: true
                focus: true
                onTextChanged: root.backend.search = text
                Accessible.name: qsTr("Search tools")

                background: Rectangle {
                    radius: 6
                    color: root.backgroundColor
                    border.width: searchField.activeFocus ? 2 : 1
                    border.color: searchField.activeFocus ? root.accentColor : root.borderColor
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 15
                    anchors.verticalCenter: parent.verticalCenter
                    text: "⌕"
                    color: root.secondaryTextColor
                    font.pixelSize: root.baseFontSize + 11
                }

                Text {
                    visible: searchField.text.length === 0
                    anchors.left: parent.left
                    anchors.leftMargin: searchField.leftPadding
                    anchors.right: parent.right
                    anchors.rightMargin: searchField.rightPadding
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Search tools and tasks…")
                    color: root.secondaryTextColor
                    font: searchField.font
                    elide: Text.ElideRight
                }

                ToolButton {
                    visible: searchField.text.length > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 5
                    anchors.verticalCenter: parent.verticalCenter
                    text: "×"
                    font.pixelSize: root.baseFontSize + 7
                    Accessible.name: qsTr("Clear search")
                    onClicked: searchField.clear()
                    background: Item {}

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }

            SecondaryButton {
                visible: root.width >= 820
                text: qsTr("Manual")
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
                onClicked: root.backend.openManual()
            }

            SecondaryButton {
                text: qsTr("About")
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
                onClicked: aboutDialog.open()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 24

        Rectangle {
            visible: !root.compactNavigation
            Layout.preferredWidth: 218
            Layout.fillHeight: true
            radius: 6
            color: root.surfaceColor
            border.color: root.borderColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 13
                spacing: 6

                Text {
                    Layout.leftMargin: 12
                    Layout.topMargin: 7
                    Layout.bottomMargin: 5
                    text: qsTr("CATEGORIES")
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(9, root.baseFontSize - 3)
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.1
                }

                Repeater {
                    model: root.backend.categories
                    CategoryButton {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        text: modelData
                        selected: searchField.text.length === 0
                                  && (root.backend.selectedCategory === modelData
                                      || (index === 0 && root.backend.selectedCategory === ""))
                        accentColor: root.accentColor
                        mutedTextColor: root.secondaryTextColor
                        hoverColor: root.accentWash
                        onClicked: {
                            searchField.clear()
                            root.backend.selectedCategory = modelData
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.borderColor
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 7

                    Text {
                        id: sidebarMenuVisibilityLabel
                        Layout.fillWidth: true
                        text: qsTr("Show tools only in MX Tools.")
                        color: root.secondaryTextColor
                        font.pixelSize: Math.max(10, root.baseFontSize - 1)
                        wrapMode: Text.Wrap
                        HoverHandler { id: sidebarMenuVisibilityLabelHover }
                        ThemeToolTip {
                            visible: sidebarMenuVisibilityLabelHover.hovered
                            text: qsTr("Hide individual tools from the applications menu")
                        }
                    }

                    ModernSwitch {
                        id: sidebarMenuVisibilitySwitch
                        checked: root.backend.hideFromMenu
                        accentColor: root.accentColor
                        inactiveColor: root.inactiveControlColor
                        knobColor: checked ? systemPalette.highlightedText : systemPalette.button
                        knobBorderColor: Qt.alpha(systemPalette.shadow, 0.25)
                        Accessible.name: qsTr("Show tools only in MX Tools.")
                        ThemeToolTip {
                            visible: sidebarMenuVisibilitySwitch.hovered
                            text: qsTr("Hide individual tools from the applications menu")
                        }
                        onToggled: root.backend.hideFromMenu = checked
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 15

            Flickable {
                id: compactCategoriesFlickable
                visible: root.compactNavigation
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                contentWidth: compactCategories.implicitWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                // Qt's default wheel handling on Flickable applies flick momentum, which on
                // touchpads keeps decelerating in the old direction after the fingers reverse
                // (you have to lift off and let it stop before it will scroll the other way).
                // Move contentX directly instead so reversing direction is immediate.
                WheelHandler {
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        const delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                        compactCategoriesFlickable.contentX = Math.max(0, Math.min(
                            Math.max(0, compactCategoriesFlickable.contentWidth - compactCategoriesFlickable.width),
                            compactCategoriesFlickable.contentX - delta))
                    }
                }

                Row {
                    id: compactCategories
                    spacing: 7
                    Repeater {
                        model: root.backend.categories
                        CategoryButton {
                            required property string modelData
                            required property int index
                            width: Math.max(92, implicitWidth)
                            text: modelData
                            selected: searchField.text.length === 0
                                      && (root.backend.selectedCategory === modelData
                                          || (index === 0 && root.backend.selectedCategory === ""))
                            accentColor: root.accentColor
                            mutedTextColor: root.secondaryTextColor
                            hoverColor: root.accentWash
                            onClicked: {
                                searchField.clear()
                                root.backend.selectedCategory = modelData
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 2
                    Text {
                        text: searchField.text.length > 0
                              ? qsTr("Search results")
                              : (root.backend.selectedCategory.length > 0
                                 ? root.backend.selectedCategory : qsTr("All tools"))
                        color: root.primaryTextColor
                        font.pixelSize: root.baseFontSize + 12
                        font.weight: Font.Bold
                    }
                    Text {
                        text: searchField.text.length > 0
                              ? qsTr("Results matching “%1”").arg(searchField.text)
                              : qsTr("Choose a tool to configure or maintain your system")
                        color: root.secondaryTextColor
                        font.pixelSize: root.baseFontSize
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: qsTr("%n tool(s)", "", toolsGrid.count)
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(10, root.baseFontSize - 1)
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 24
                    Layout.leftMargin: 5
                    Layout.rightMargin: 5
                    color: root.borderColor
                }

                Text {
                    Layout.maximumWidth: 130
                    text: qsTr("Condensed view")
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(10, root.baseFontSize - 1)
                    elide: Text.ElideRight
                }
                ModernSwitch {
                    id: condensedSwitch
                    checked: root.condensedView
                    accentColor: root.accentColor
                    inactiveColor: root.inactiveControlColor
                    knobColor: checked ? systemPalette.highlightedText : systemPalette.button
                    knobBorderColor: Qt.alpha(systemPalette.shadow, 0.25)
                    Accessible.name: qsTr("Use condensed tool view")
                    ThemeToolTip {
                        visible: condensedSwitch.hovered
                        text: qsTr("Show more tools at once")
                    }
                    onToggled: root.condensedView = checked
                }
            }

            GridView {
                id: toolsGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
                model: root.backend
                cellWidth: width / Math.max(1, Math.floor(width / (root.condensedView ? 230 : 300)))
                cellHeight: root.condensedView ? 112 : 154

                onCellWidthChanged: Qt.callLater(toolsGrid.returnToBounds)
                onCellHeightChanged: Qt.callLater(toolsGrid.returnToBounds)

                // See the comment on the compact category Flickable above: bypass the default
                // flick-momentum wheel handling so touchpad scrolling can reverse direction
                // immediately instead of needing a full stop first.
                WheelHandler {
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        const minimumY = toolsGrid.originY
                        const maximumY = Math.max(minimumY,
                                                  minimumY + toolsGrid.contentHeight - toolsGrid.height)
                        toolsGrid.contentY = Math.max(minimumY, Math.min(
                            maximumY,
                            toolsGrid.contentY - event.angleDelta.y))
                    }
                }

                delegate: ToolCard {
                    required property string name
                    required property string comment
                    required property string category
                    required property string fileName

                    width: toolsGrid.cellWidth - 12
                    height: toolsGrid.cellHeight - 12
                    toolName: name
                    description: comment
                    categoryName: category
                    condensed: root.condensedView
                    surfaceColor: root.surfaceColor
                    hoverSurfaceColor: root.raisedSurfaceColor
                    primaryTextColor: root.primaryTextColor
                    secondaryTextColor: root.secondaryTextColor
                    accentColor: root.accentColor
                    borderColor: root.borderColor
                    onClicked: root.backend.launch(fileName)
                }

                displaced: Transition {
                    NumberAnimation { properties: "x,y"; duration: 160; easing.type: Easing.OutCubic }
                }

                Text {
                    visible: toolsGrid.count === 0
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40, 420)
                    text: qsTr("No tools found\nTry a different search or category.")
                    color: root.secondaryTextColor
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: root.baseFontSize + 3
                    lineHeight: 1.5
                }
            }

            RowLayout {
                visible: root.compactNavigation
                Layout.fillWidth: true
                spacing: 7

                Item { Layout.fillWidth: true }
                Text {
                    id: compactMenuVisibilityLabel
                    text: qsTr("Show tools only in MX Tools.")
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(10, root.baseFontSize - 1)
                    HoverHandler { id: compactMenuVisibilityLabelHover }
                    ThemeToolTip {
                        visible: compactMenuVisibilityLabelHover.hovered
                        text: qsTr("Hide individual tools from the applications menu")
                    }
                }
                ModernSwitch {
                    id: compactMenuVisibilitySwitch
                    checked: root.backend.hideFromMenu
                    accentColor: root.accentColor
                    inactiveColor: root.inactiveControlColor
                    knobColor: checked ? systemPalette.highlightedText : systemPalette.button
                    knobBorderColor: Qt.alpha(systemPalette.shadow, 0.25)
                    Accessible.name: qsTr("Show tools only in MX Tools.")
                    ThemeToolTip {
                        visible: compactMenuVisibilitySwitch.hovered
                        text: qsTr("Hide individual tools from the applications menu")
                    }
                    onToggled: root.backend.hideFromMenu = checked
                }
            }
        }
    }

    Dialog {
        id: aboutDialog
        modal: true
        width: 440
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        title: qsTr("About MX Tools")
        footer: DialogButtonBox {
            standardButtons: DialogButtonBox.Close
            alignment: Qt.AlignRight
            padding: 12
            background: Item {}
            delegate: SecondaryButton {
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
            }
            onRejected: aboutDialog.reject()
        }

        background: Rectangle {
            color: root.surfaceColor
            radius: 6
            border.color: root.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 14
            Image {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 72
                Layout.preferredHeight: 72
                source: "../icons/logo.svg"
                sourceSize: Qt.size(96, 96)
                fillMode: Image.PreserveAspectFit
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("MX Tools")
                color: root.primaryTextColor
                font.pixelSize: root.baseFontSize + 11
                font.weight: Font.Bold
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Version %1").arg(root.version)
                color: root.secondaryTextColor
                font.pixelSize: root.baseFontSize
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("A focused collection of configuration and maintenance tools for MX Linux.")
                color: root.primaryTextColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                font.pixelSize: root.baseFontSize + 1
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                SecondaryButton {
                    text: qsTr("Website")
                    textColor: root.primaryTextColor
                    hoverColor: root.accentWash
                    borderColor: root.borderColor
                    accentColor: root.accentColor
                    onClicked: root.backend.openWebsite()
                }
                SecondaryButton {
                    text: qsTr("License")
                    textColor: root.primaryTextColor
                    hoverColor: root.accentWash
                    borderColor: root.borderColor
                    accentColor: root.accentColor
                    onClicked: root.backend.openLicense()
                }
                SecondaryButton {
                    text: qsTr("Changelog")
                    textColor: root.primaryTextColor
                    hoverColor: root.accentWash
                    borderColor: root.borderColor
                    accentColor: root.accentColor
                    onClicked: root.backend.openChangelog()
                }
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Copyright © MX Linux")
                color: root.secondaryTextColor
                font.pixelSize: Math.max(10, root.baseFontSize - 2)
            }
        }
    }

    Dialog {
        id: errorDialog
        property string message: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        footer: DialogButtonBox {
            standardButtons: DialogButtonBox.Ok
            alignment: Qt.AlignRight
            padding: 12
            background: Item {}
            delegate: SecondaryButton {
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
            }
            onAccepted: errorDialog.accept()
        }
        contentItem: Text {
            text: errorDialog.message
            color: root.primaryTextColor
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: documentDialog
        property string content: ""
        modal: true
        width: Math.min(root.width - 80, 760)
        height: Math.min(root.height - 80, 560)
        anchors.centerIn: Overlay.overlay
        footer: DialogButtonBox {
            standardButtons: DialogButtonBox.Close
            alignment: Qt.AlignRight
            padding: 12
            background: Item {}
            delegate: SecondaryButton {
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
            }
            onRejected: documentDialog.reject()
        }

        contentItem: ScrollView {
            TextArea {
                text: documentDialog.content
                color: root.primaryTextColor
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                background: Rectangle { color: root.backgroundColor; radius: 6 }
            }
        }
    }

    Connections {
        target: root.backend
        function onErrorOccurred(title, message) {
            errorDialog.title = title
            errorDialog.message = message
            errorDialog.open()
        }
        function onDocumentReady(title, content) {
            documentDialog.title = title
            documentDialog.content = content
            documentDialog.open()
        }
    }

    Shortcut { sequence: StandardKey.Find; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "Escape"; onActivated: searchField.text.length > 0 ? searchField.clear() : root.close() }
}
