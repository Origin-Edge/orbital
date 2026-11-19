import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import Orbital

// ORBITAL Canlı: tam pencere sesli konuşma ekranı. Konuş, susunca ORBITAL yanıtlar ve okur.
// Kamera açıkken sorduğunda (ya da "Şimdi bak" / sürekli izlemede) görüntüye bakar.
Popup {
    id: ekran
    function ac() { open(); Canli.baslat() }

    parent: Overlay.overlay
    x: 0; y: 0
    width: parent.width
    height: parent.height
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onClosed: { Canli.bitir(); kamera.active = false; Canli.goruntuAcik = false }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }

    readonly property var durumYazi: ({
        bekliyor: "Başlamak için küreye dokun",
        dinliyor: Canli.dinle ? "Dinliyorum…" : "Mikrofon kapalı — yazarak sorabilirsin",
        kayit: "Seni duyuyorum…",
        dusunuyor: "Düşünüyorum…",
        konusuyor: "Konuşuyor — sözünü kesmek için dokun"
    })

    // Kamera: önizleme + son kareyi C++'a verme
    CaptureSession {
        id: oturum
        camera: Camera { id: kamera; active: false }
        videoOutput: onizleme
    }
    Connections {
        target: Canli
        function onKareIste() { Canli.kareAl(onizleme.videoSink) }
    }

    background: Rectangle { color: Theme.bg }

    contentItem: Item {
        // Üst çubuk
        RowLayout {
            id: ust
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
            spacing: 10
            Icon { name: "sparkle"; size: 20; color: Theme.accent }
            Label { text: "ORBITAL Canlı"; color: Theme.text; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; font.family: Theme.font }
            Item { Layout.fillWidth: true }
            Label { text: "Sürekli izle"; color: Canli.goruntuAcik ? Theme.textMuted : Theme.textFaint; font.pixelSize: Theme.fontSmall; font.family: Theme.font }
            OSwitch {
                id: izleAnahtar
                checked: Canli.izle
                enabled: Canli.goruntuAcik
                onToggled: Canli.izle = checked
                ToolTip.visible: hovered
                ToolTip.text: "Kamerayı belirli aralıkla izler, yalnızca bir şey değişince söyler"
            }
            ComboBox {
                model: ["8 sn", "15 sn", "30 sn"]
                currentIndex: Canli.izleAralik <= 8 ? 0 : Canli.izleAralik <= 15 ? 1 : 2
                enabled: Canli.goruntuAcik
                implicitWidth: 92
                onActivated: idx => Canli.izleAralik = [8, 15, 30][idx]
            }
            IconButton { iconName: "trash"; tip: "Konuşmayı temizle"; onClicked: Canli.temizle() }
            IconButton { iconName: "x"; tip: "Kapat (Esc)"; onClicked: ekran.close() }
        }

        // Kamera önizlemesi (sağ üst)
        Rectangle {
            id: kameraKutu
            visible: Canli.goruntuAcik
            anchors { right: parent.right; top: ust.bottom; margins: 16 }
            width: Math.min(320, parent.width * 0.32)
            height: width * 9 / 16 + 36
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.border
            clip: true
            VideoOutput {
                id: onizleme
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: parent.width * 9 / 16
                fillMode: VideoOutput.PreserveAspectCrop
                transform: Scale { origin.x: onizleme.width / 2; xScale: -1 }
            }
            RowLayout {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 6 }
                Label { text: "Kameran"; color: Theme.textMuted; font.pixelSize: Theme.fontCaption; Layout.fillWidth: true; leftPadding: 4 }
                OButton { text: "Şimdi bak"; onClicked: Canli.simdiBak() }
            }
        }

        // Küre + durum + yazılar
        ColumnLayout {
            anchors { left: parent.left; right: parent.right; top: ust.bottom; bottom: alt.top; margins: 24 }
            spacing: 18

            Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }

            Rectangle {
                id: kure
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(220, ekran.height * 0.28)
                Layout.preferredHeight: Layout.preferredWidth
                radius: width / 2
                scale: (Canli.durum === "kayit" || Canli.durum === "dinliyor") ? 1 + Math.min(0.25, Canli.seviye * 2.2) : 1
                Behavior on scale { NumberAnimation { duration: 80 } }
                opacity: Canli.aktif ? 1 : 0.55
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Canli.durum === "konusuyor" ? "#a5f3fc" : "#bfdbfe" }
                    GradientStop { position: 0.45; color: Canli.durum === "konusuyor" ? "#0891b2" : Canli.durum === "dusunuyor" ? "#38bdf8" : "#3b82f6" }
                    GradientStop { position: 1.0; color: Canli.durum === "konusuyor" ? "#164e63" : "#1e3a8a" }
                }
                RotationAnimator on rotation { running: Canli.durum === "dusunuyor"; from: 0; to: 360; duration: 1600; loops: Animation.Infinite }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!Canli.aktif) Canli.baslat()
                        else if (Canli.durum === "konusuyor") Canli.sozuKes()
                    }
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: ekran.durumYazi[Canli.durum] || ""
                color: Theme.textMuted
                font.pixelSize: Theme.fontUi
                font.family: Theme.font
            }

            ListView {
                id: yazilar
                Layout.fillWidth: true
                Layout.maximumWidth: 720
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: Math.min(contentHeight, ekran.height * 0.34)
                clip: true
                spacing: 12
                model: Canli.mesajlar
                onCountChanged: positionViewAtEnd()
                delegate: Column {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 4
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: modelData.rol === "kullanici" ? "“" + modelData.metin + "”" : modelData.metin
                        color: modelData.rol === "kullanici" ? Theme.textFaint : Theme.text
                        font.pixelSize: modelData.rol === "kullanici" ? Theme.fontUi : Theme.fontBody + 1
                        font.family: Theme.font
                        textFormat: Text.PlainText
                    }
                    Flow {
                        width: parent.width
                        spacing: 10
                        visible: (modelData.kaynaklar || []).length > 0
                        Repeater {
                            model: modelData.kaynaklar || []
                            Label {
                                required property var modelData
                                text: "<a href=\"" + modelData.url + "\">" + String(modelData.url).replace(/^https?:\/\/(www\.)?/, "").split("/")[0] + "</a>"
                                textFormat: Text.StyledText
                                linkColor: Theme.accent
                                font.pixelSize: Theme.fontCaption
                                onLinkActivated: link => Qt.openUrlExternally(link)
                            }
                        }
                    }
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: Canli.uyari.length > 0
                text: Canli.uyari
                color: Theme.danger
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.maximumWidth: 600
                horizontalAlignment: Text.AlignHCenter
            }

            Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }
        }

        // Alt: yazı kutusu + kontroller
        ColumnLayout {
            id: alt
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 20 }
            spacing: 12

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(600, ekran.width - 40)
                Layout.maximumWidth: 600
                spacing: 8
                OTextField {
                    id: yazi
                    Layout.fillWidth: true
                    placeholderText: "Ya da yaz…"
                    onAccepted: { Canli.yaziyla(text); text = "" }
                }
                OButton { text: "Gönder"; variant: "primary"; onClicked: { Canli.yaziyla(yazi.text); yazi.text = "" } }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                component Kontrol: RoundButton {
                    id: kd
                    property string ikon
                    property bool acik: false
                    property bool tehlike: false
                    implicitWidth: 52; implicitHeight: 52
                    background: Rectangle {
                        radius: width / 2
                        color: kd.tehlike ? Theme.danger : kd.acik ? Theme.accent : (kd.hovered ? Theme.hover : Theme.surface)
                        border.color: kd.acik || kd.tehlike ? "transparent" : Theme.border
                    }
                    contentItem: Item { Icon { anchors.centerIn: parent; name: kd.ikon; size: 22; color: kd.acik || kd.tehlike ? Theme.textOnAccent : Theme.text } }
                }
                Kontrol {
                    ikon: "mic"; acik: Canli.aktif && Canli.dinle
                    ToolTip.visible: hovered; ToolTip.text: Canli.dinle ? "Mikrofonu kapat" : "Mikrofonu aç"
                    onClicked: { if (!Canli.aktif) Canli.baslat(); else Canli.dinle = !Canli.dinle }
                }
                Kontrol {
                    ikon: "camera"; acik: Canli.goruntuAcik
                    ToolTip.visible: hovered; ToolTip.text: Canli.goruntuAcik ? "Kamerayı kapat" : "Kamerayı aç"
                    onClicked: { kamera.active = !Canli.goruntuAcik; Canli.goruntuAcik = kamera.active }
                }
                Kontrol {
                    ikon: "volume"; acik: Canli.sesli
                    ToolTip.visible: hovered; ToolTip.text: "Yanıtları sesli oku"
                    onClicked: Canli.sesli = !Canli.sesli
                }
                Kontrol {
                    ikon: "x"; tehlike: true
                    ToolTip.visible: hovered; ToolTip.text: "Bitir"
                    onClicked: ekran.close()
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "Konuş, susunca ORBITAL yanıtlar. Kamera yalnızca sorduğunda ya da “Sürekli izle” açıkken gönderilir."
                color: Theme.textFaint
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                Layout.maximumWidth: ekran.width - 40
            }
        }
    }
}
