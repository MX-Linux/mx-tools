pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// One CategoryButton per category, for either the sidebar column or the compact row.
Repeater {
    id: repeater

    required property var backend
    property bool searching: false
    property bool fillWidth: false

    signal categoryChosen(string category)

    model: backend.categories

    CategoryButton {
        id: button
        required property string modelData
        required property int index
        Layout.fillWidth: repeater.fillWidth
        width: Math.max(92, implicitWidth)
        text: button.modelData
        // "All tools" (the first entry) stands for no selected category.
        selected: !repeater.searching
                  && (repeater.backend.selectedCategory === button.modelData
                      || (button.index === 0 && repeater.backend.selectedCategory === ""))
        onClicked: repeater.categoryChosen(button.modelData)
    }
}
