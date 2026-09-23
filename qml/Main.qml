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
    readonly property color primaryTextColor: systemPalette.text
    readonly property color secondaryTextColor: Qt.alpha(systemPalette.text, 0.82)
    readonly property color borderColor: Qt.alpha(systemPalette.text, 0.18)
    readonly property color accentColor: systemPalette.highlight
    readonly property color accentWash: Qt.alpha(systemPalette.highlight, 0.13)
    readonly property real baseFontSize: Application.font.pixelSize > 0 ? Application.font.pixelSize : 13
    readonly property bool compactNavigation: width < 900
    property bool condensedView: false
    property bool hideCategories: false
    required property var backend
    required property string version

    Settings {
        category: "MainWindow"
        property alias windowX: root.x
        property alias windowY: root.y
        property alias windowWidth: root.width
        property alias windowHeight: root.height
        property alias condensedView: root.condensedView
        property alias hideCategories: root.hideCategories
    }

    // Monitors can change between runs, so a restored position may be off every screen.
    // Settings has restored it by now; unless the title bar is on some screen, fit the
    // window to its screen and center it. (On Wayland the compositor places windows.)
    Component.onCompleted: {
        const titleBarVisible = Application.screens.some(screen =>
            root.x + root.width > screen.virtualX + 100 && root.x < screen.virtualX + screen.width - 100
            && root.y >= screen.virtualY && root.y < screen.virtualY + screen.height - 50)
        if (!titleBarVisible) {
            root.width = Math.min(root.width, Screen.width)
            root.height = Math.min(root.height, Screen.height)
            root.x = Screen.virtualX + (Screen.width - root.width) / 2
            root.y = Screen.virtualY + (Screen.height - root.height) / 2
        }
    }

    function chooseCategory(category) {
        searchField.clear()
        root.backend.selectedCategory = category
    }

    // Hiding the category list also hides the only way back to "All tools",
    // so drop any active filter instead of stranding the user in a subset.
    onHideCategoriesChanged: {
        if (root.hideCategories) {
            root.backend.selectedCategory = ""
        }
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

                // A drawn magnifier: the U+2315 glyph used before is missing from many fonts.
                Item {
                    anchors.left: parent.left
                    anchors.leftMargin: 15
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18

                    Rectangle {
                        width: 13
                        height: 13
                        radius: width / 2
                        color: "transparent"
                        border.width: 2
                        border.color: root.secondaryTextColor
                    }
                    Rectangle {
                        x: 10
                        y: 13
                        width: 8
                        height: 2
                        radius: 1
                        rotation: 45
                        transformOrigin: Item.Left
                        color: root.secondaryTextColor
                    }
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
                onClicked: root.backend.openManual()
            }

            SecondaryButton {
                text: qsTr("About")
                onClicked: aboutDialog.open()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 24

        Rectangle {
            visible: !root.compactNavigation && !root.hideCategories
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

                CategoryRepeater {
                    backend: root.backend
                    searching: searchField.text.length > 0
                    fillWidth: true
                    onCategoryChosen: (category) => root.chooseCategory(category)
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.borderColor
                }

                MenuVisibilityToggle {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    backend: root.backend
                    wrapLabel: true
                    fontPixelSize: Math.max(10, root.baseFontSize - 1)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 15

            Flickable {
                id: compactCategoriesFlickable
                visible: root.compactNavigation && !root.hideCategories
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
                    CategoryRepeater {
                        backend: root.backend
                        searching: searchField.text.length > 0
                        onCategoryChosen: (category) => root.chooseCategory(category)
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
                    Accessible.name: qsTr("Use condensed tool view")
                    ThemeToolTip {
                        visible: condensedSwitch.hovered
                        text: qsTr("Show more tools at once")
                    }
                    onToggled: root.condensedView = checked
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
                    text: qsTr("Hide categories")
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(10, root.baseFontSize - 1)
                    elide: Text.ElideRight
                }
                ModernSwitch {
                    id: hideCategoriesSwitch
                    checked: root.hideCategories
                    Accessible.name: qsTr("Hide the category list")
                    ThemeToolTip {
                        visible: hideCategoriesSwitch.hovered
                        text: qsTr("Use the whole window for tools")
                    }
                    onToggled: root.hideCategories = checked
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

                // The grid is a single Tab stop: only the delegates in view exist, so tabbing
                // through cards couldn't reach the rest or scroll. The arrow keys move the
                // current card instead (GridView scrolls to it), and that card holds the focus
                // inside the grid, so its focus ring, Return and accessibility work as before.
                activeFocusOnTab: true
                keyNavigationEnabled: true

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
                    required property int index

                    focus: GridView.isCurrentItem
                    activeFocusOnTab: false
                    // A card focused by the mouse becomes the current one for the arrow keys.
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            toolsGrid.currentIndex = index
                        }
                    }

                    width: toolsGrid.cellWidth - 12
                    height: toolsGrid.cellHeight - 12
                    toolName: name
                    description: comment
                    categoryName: category
                    condensed: root.condensedView
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
                visible: root.compactNavigation || root.hideCategories
                Layout.fillWidth: true
                spacing: 7

                Item { Layout.fillWidth: true }
                MenuVisibilityToggle {
                    backend: root.backend
                    fontPixelSize: Math.max(10, root.baseFontSize - 1)
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
            delegate: SecondaryButton {}
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
                    onClicked: root.backend.openWebsite()
                }
                SecondaryButton {
                    text: qsTr("License")
                    onClicked: root.backend.openLicense()
                }
                SecondaryButton {
                    text: qsTr("Changelog")
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
        // Errors queue up instead of replacing one that is still shown, and are shown
        // oldest first; an item leaves the queue only when it is displayed.
        property var pending: []
        function show(title, message) {
            pending = pending.concat([{ title: title, message: message }])
            showNext()
        }
        function showNext() {
            if (visible || pending.length === 0) {
                return
            }
            const next = pending[0]
            pending = pending.slice(1)
            errorDialog.title = next.title
            errorDialog.message = next.message
            open()
        }
        modal: true
        width: Math.min(root.width - 80, 480)
        anchors.centerIn: Overlay.overlay
        onClosed: Qt.callLater(showNext)
        footer: DialogButtonBox {
            standardButtons: DialogButtonBox.Ok
            alignment: Qt.AlignRight
            padding: 12
            background: Item {}
            delegate: SecondaryButton {}
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
            delegate: SecondaryButton {}
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
            errorDialog.show(title, message)
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
