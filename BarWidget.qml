import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui

// Gimbal's settings, in the bar.
//
// A GUI rather than a TUI, for one reason: this is the tablet's settings
// screen, and the tablet has no keyboard out unless you ask for one. Sliders
// and switches you can hit with a thumb are the whole point. It is built on
// Omarchy's own kit -- the same PanelSlider, ToggleSwitch and section headers
// the Wi-Fi panel uses -- so it inherits the theme and needs nothing new
// installed.
//
// Settings live in ~/.config/omarchy/gimbal.json rather than in this plugin's
// shell.json entry, because shell.json is Omarchy's file and a plugin that
// rewrites it will eventually lose a race with the shell. Panel.qml watches
// our file and layers it over whatever shell.json says, so a value set here
// wins and anything left unset falls back.
Panel {
    id: root

    moduleName: "io.github.mechanicsunlocked.gimbal"
    ipcTarget: "gimbal"

    readonly property color foreground: bar ? bar.foreground : Color.foreground
    readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family
    readonly property color dim: Qt.darker(foreground, 1.55)

    readonly property string home: Quickshell.env("HOME") || ""
    readonly property string configPath: home + "/.config/omarchy/gimbal.json"
    readonly property string runtimeDir: Quickshell.env("XDG_RUNTIME_DIR") || "/tmp"
    readonly property string modePath: runtimeDir + "/gimbal-mode"

    property string tabletState: ""
    readonly property bool folded: tabletState !== "laptop"

    property var conf: ({})

    // Kept identical to Panel.qml's own defaults. They are repeated rather
    // than shared because the two are separate QML instances with no object
    // in common; the file between them carries values, not defaults.
    readonly property var fallback: ({
            "mode": "both",
            "swipeGutter": 30,
            "swipeUp": "@keyboard",
            "swipeDown": "omarchy-menu",
            "swipeRight": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"e-1\" })'",
            "swipeLeft": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"+1\" })'",
            "blockOnMoonlight": true
        })

    readonly property var gestures: [
        {
            key: "swipeUp",
            label: "Swipe up"
        },
        {
            key: "swipeDown",
            label: "Swipe down"
        },
        {
            key: "swipeLeft",
            label: "Swipe left"
        },
        {
            key: "swipeRight",
            label: "Swipe right"
        }
    ]

    function value(key) {
        var v = root.conf ? root.conf[key] : undefined;
        return (v === undefined || v === null) ? root.fallback[key] : v;
    }

    // Written whole every time. The file is six keys long, so there is nothing
    // to gain from a partial update and something to lose: a merge would have
    // to guess what an absent key means.
    function setValue(key, v) {
        var next = {};
        for (var k in root.conf)
            next[k] = root.conf[k];
        next[key] = v;
        root.conf = next;
        configFile.setText(JSON.stringify(next, null, 2) + "\n");
    }

    FileView {
        id: configFile

        path: root.configPath
        watchChanges: true
        printErrors: false

        onFileChanged: reload()
        onLoaded: {
            try {
                root.conf = JSON.parse(text()) || ({});
            } catch (e) {}
        }
        onLoadFailed: root.conf = ({})
    }

    FileView {
        id: modeFile

        path: root.modePath
        watchChanges: true
        printErrors: false

        onFileChanged: reload()
        onLoaded: root.tabletState = text().trim()
        onLoadFailed: root.tabletState = ""
    }

    BarIconButton {
        id: button

        anchors.fill: parent
        bar: root.bar
        text: "\uf10a"
        tooltipText: "Gimbal"
        onPressed: function (b) {
            root.toggle();
        }
    }

    KeyboardPanel {
        id: panel

        anchorItem: button
        owner: root
        bar: root.bar
        open: root.opened
        focusTarget: keyCatcher
        contentWidth: panel.fittedContentWidth(Style.space(400))
        contentHeight: panel.fittedContentHeight(column.implicitHeight)

        PanelKeyCatcher {
            id: keyCatcher

            anchors.fill: parent
            onCloseRequested: root.close()
            onTabRequested: function (direction) {
                root.switchPanel(direction);
            }

            Column {
                id: column

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: Style.space(12)

                // ---------- Header ----------
                Item {
                    width: parent.width
                    implicitHeight: Math.max(title.implicitHeight, stateLabel.implicitHeight)

                    Text {
                        id: title

                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Gimbal"
                        color: root.foreground
                        font.family: root.fontFamily
                        font.pixelSize: Style.font.subtitle
                    }

                    Text {
                        id: stateLabel

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.folded ? "Tablet" : "Laptop"
                        color: root.folded ? Color.accent : root.dim
                        font.family: root.fontFamily
                        font.pixelSize: Style.font.caption
                    }
                }

                PanelSeparator {
                    width: parent.width
                    foreground: root.foreground
                }

                // ---------- Interaction ----------
                PanelSectionHeader {
                    text: "INTERACTION"
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                }

                ButtonGroup {
                    width: parent.width
                    spacing: Style.space(6)
                    foreground: root.foreground
                    background: root.bar ? root.bar.background : Color.background
                    fontFamily: root.fontFamily
                    options: [
                        {
                            value: "edges",
                            label: "Edges",
                            tooltip: "Screen-edge strips only"
                        },
                        {
                            value: "pads",
                            label: "Pads",
                            tooltip: "The two thumb pads only"
                        },
                        {
                            value: "both",
                            label: "Both",
                            tooltip: "Edge strips and thumb pads"
                        }
                    ]
                    value: String(root.value("mode"))
                    onChanged: function (v) {
                        root.setValue("mode", v);
                    }
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Pads reserve nothing, so turning the edges off gives the keyboard its full width back."
                    color: root.dim
                    font.family: root.fontFamily
                    font.pixelSize: Style.font.caption
                }

                // ---------- Gutter width ----------
                Item {
                    width: parent.width
                    implicitHeight: gutterLabel.implicitHeight
                    opacity: root.value("mode") === "pads" ? 0.4 : 1.0

                    PanelSectionHeader {
                        id: gutterLabel

                        anchors.left: parent.left
                        text: "EDGE WIDTH"
                        foreground: root.foreground
                        fontFamily: root.fontFamily
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: gutterLabel.verticalCenter
                        anchors.verticalCenterOffset: Math.round(gutterLabel.topPadding / 2)
                        text: Math.round(gutterSlider.liveValue) + " px"
                        color: root.dim
                        font.family: root.fontFamily
                        font.pixelSize: Style.font.caption
                    }
                }

                PanelSlider {
                    id: gutterSlider

                    width: parent.width
                    height: Style.space(24)
                    bar: root.bar
                    enabled: root.value("mode") !== "pads"
                    opacity: enabled ? 1.0 : 0.4
                    minimum: 0
                    maximum: 60
                    step: 2
                    integer: true
                    value: Number(root.value("swipeGutter"))
                    // Written on release rather than on every sample: the
                    // strips rebuild on each change, and rebuilding a layer
                    // surface sixty times across one drag is visible.
                    onReleased: function (v) {
                        root.setValue("swipeGutter", Math.round(v));
                    }
                }

                PanelSeparator {
                    width: parent.width
                    foreground: root.foreground
                }

                // ---------- Gestures ----------
                PanelSectionHeader {
                    text: "GESTURES"
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                }

                Repeater {
                    model: root.gestures

                    delegate: Column {
                        required property var modelData

                        width: column.width
                        spacing: Style.space(3)

                        Text {
                            text: parent.modelData.label
                            color: root.dim
                            font.family: root.fontFamily
                            font.pixelSize: Style.font.caption
                        }

                        TextField {
                            width: parent.width
                            text: String(root.value(parent.modelData.key))
                            placeholderText: String(root.fallback[parent.modelData.key])
                            foreground: root.foreground
                            font.family: root.fontFamily
                            font.pixelSize: Style.font.caption
                            onEditingFinished: root.setValue(parent.modelData.key, text)
                        }
                    }
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Any shell command. @keyboard is the one built-in: it shows and hides the on-screen keyboard."
                    color: root.dim
                    font.family: root.fontFamily
                    font.pixelSize: Style.font.caption
                }

                PanelSeparator {
                    width: parent.width
                    foreground: root.foreground
                }

                // ---------- Gaming ----------
                PanelSectionHeader {
                    text: "GAMING"
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                }

                Item {
                    width: parent.width
                    implicitHeight: moonlightLabel.implicitHeight

                    Text {
                        id: moonlightLabel

                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Hold the keyboard back for Moonlight"
                        color: root.foreground
                        font.family: root.fontFamily
                        font.pixelSize: Style.font.caption
                    }

                    ToggleSwitch {
                        anchors.right: parent.right
                        anchors.verticalCenter: moonlightLabel.verticalCenter
                        trackHeight: Math.round(moonlightLabel.font.pixelSize * 1.2)
                        cursorPad: Style.space(3)
                        foreground: root.foreground
                        checked: root.value("blockOnMoonlight") === true
                        onToggled: root.setValue("blockOnMoonlight", !(root.value("blockOnMoonlight") === true))
                    }
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "While a Moonlight window is open, nothing summons the keyboard -- not a swipe, not the button, not SUPER+B. The gestures still work."
                    color: root.dim
                    font.family: root.fontFamily
                    font.pixelSize: Style.font.caption
                }
            }
        }
    }
}
