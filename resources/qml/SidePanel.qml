import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Orbital

// Right-hand panel (toggled from the header): where this chat runs and what it uses.
Rectangle {
    id: root
    color: Theme.sidebar
    signal close()
    signal openSettings(string page)

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.border
    }

    component Heading: Text {
        Layout.fillWidth: true
        Layout.topMargin: 14
        Layout.bottomMargin: 4
        Layout.leftMargin: 4
        font.family: Theme.font
        font.pixelSize: Theme.fontCaption
        font.weight: Font.DemiBold
        color: Theme.textFaint
    }

    component InfoRow: AbstractButton {
        id: info
        property string iconName
        property string detail
        property bool clickable: false
        Layout.fillWidth: true
        implicitHeight: detail.length ? 46 : 34
        hoverEnabled: clickable
        enabled: clickable
        background: Rectangle { radius: Theme.radiusSm; color: info.hovered ? Theme.hover : "transparent" }
        contentItem: RowLayout {
            spacing: 10
            Icon { Layout.leftMargin: 6; name: info.iconName; size: 16; color: Theme.textMuted }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: info.text
                    elide: Text.ElideRight
                    font.family: Theme.font
                    font.pixelSize: Theme.fontUi
                    color: Theme.text
                }
                Text {
                    Layout.fillWidth: true
                    visible: info.detail.length > 0
                    text: info.detail
                    elide: Text.ElideMiddle
                    font.family: Theme.font
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textFaint
                }
            }
            Icon { visible: info.clickable; Layout.rightMargin: 6; name: "external"; size: 13; color: Theme.textFaint }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        anchors.leftMargin: 13
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                text: qsTr("Oturum")
                font.family: Theme.font
                font.pixelSize: Theme.fontUi + 1
                font.weight: Font.DemiBold
                color: Theme.text
            }
            IconButton { iconName: "x"; iconSize: 15; tip: "Paneli kapat"; onClicked: root.close() }
        }

        Heading { text: App.isNoFolder ? qsTr("Mod") : qsTr("Proje") }
        InfoRow {
            iconName: App.isNoFolder ? "chat" : "folder"
            text: App.isNoFolder ? qsTr("Sohbet (klasörsüz)") : App.projectName
            detail: App.isNoFolder ? qsTr("Dosyalara dokunmadan sohbet") : App.projectPath
            clickable: !App.isNoFolder
            onClicked: App.openPath(App.projectPath)
        }

        Heading { text: qsTr("Motor ve model") }
        InfoRow {
            iconName: "cpu"
            text: App.isNoFolder ? qsTr("ORBITAL Web") : App.engineId === "opencode" ? qsTr("ORBITAL") : qsTr("Orbital Core")
            detail: !App.engineReady ? qsTr("Hazırlanıyor…") : App.isNoFolder ? qsTr("yz.oedge.xyz üzerinden") : qsTr("Bu bilgisayarda")
            clickable: true
            onClicked: root.openSettings("general")
        }
        InfoRow {
            iconName: App.isCurrentModelLocked ? "lock" : "sparkle"
            text: App.modelLabel
            detail: App.isCurrentModelLocked ? qsTr("Limit doldu, kendiliğinden açılacak") : App.modelRef
        }

        Heading { text: qsTr("Kullanım") }
        InfoRow {
            iconName: "clock"
            text: App.usage.length ? App.usage : qsTr("Bu sohbette henüz istek yok")
        }

        Heading { visible: App.attachedSkills.length > 0; text: qsTr("İliştirilen skill'ler") }
        Repeater {
            model: App.attachedSkills
            delegate: InfoRow {
                required property string modelData
                iconName: "sparkle"
                text: modelData
            }
        }

        Heading { text: qsTr("Hesap") }
        InfoRow {
            iconName: "user"
            text: App.accountSignedIn ? (App.accountName || App.accountEmail) : qsTr("Giriş yapılmadı")
            detail: App.accountSignedIn ? App.accountEmail : qsTr("EdgeAccount ile giriş yapılabilir")
            clickable: !App.accountSignedIn && !App.accountBusy
            onClicked: App.accountSignIn()
        }

        Item { Layout.fillHeight: true }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            text: qsTr("Orbital v") + App.appVersion
            font.family: Theme.font
            font.pixelSize: Theme.fontCaption
            color: Theme.textFaint
        }
    }
}
