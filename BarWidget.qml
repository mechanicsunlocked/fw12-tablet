import QtQuick
import QtQuick.Shapes
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

    // The Laptop 12's own sage, and a red far enough from it to be told apart
    // at a glance on a screen you are holding at arm's length. Fixed rather
    // than themed: on and off have to stay legible whatever the wallpaper is
    // doing, and these two are the machine's own colours.
    readonly property color sage: "#9CAF88"
    readonly property color offRed: "#C2554D"

    // edges and pads are stored as their own switches. A config written before
    // they were split says "mode" instead, so that is still understood.
    function onOff(key) {
        var v = root.conf ? root.conf[key] : undefined;
        if (v !== undefined && v !== null)
            return v === true;
        var m = String((root.conf && root.conf["mode"]) || "both");
        if (key === "edges")
            return m !== "pads";
        if (key === "pads")
            return m !== "edges";
        return true;
    }

    readonly property string home: Quickshell.env("HOME") || ""
    readonly property string configPath: home + "/.config/omarchy/gimbal.json"
    readonly property string runtimeDir: Quickshell.env("XDG_RUNTIME_DIR") || "/tmp"
    readonly property string modePath: runtimeDir + "/gimbal-mode"
    readonly property string oskStatePath: runtimeDir + "/gimbal-osk"

    // The bar host sizes a slot around whatever the widget asks for, so two
    // buttons need a stated width; a single one could get away with filling
    // the default slot.
    readonly property bool vertical: bar ? bar.vertical : false
    readonly property int barSize: bar ? bar.barSize : Style.bar.sizeHorizontal

    implicitWidth: root.vertical ? root.barSize : buttons.implicitWidth
    implicitHeight: root.vertical ? buttons.implicitHeight : root.barSize

    property bool keyboardShown: false

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
            "swipeRight": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"r-1\" })'",
            "swipeLeft": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"r+1\" })'",
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

    // Panel.qml owns the keyboard and writes a byte here when it comes and
    // goes. Watching a file rather than reaching into the shell's map of
    // loaded panels keeps the dependency between the two halves down to one
    // path, and the button lights up the moment the keyboard does however it
    // was summoned -- bar, swipe or SUPER+B.
    FileView {
        id: oskStateFile

        path: root.oskStatePath
        watchChanges: true
        printErrors: false

        onFileChanged: reload()
        onLoaded: root.keyboardShown = text().trim() === "1"
        onLoadFailed: root.keyboardShown = false
    }

    // Keyboard on the left, settings on the right, in that order because the
    // keyboard is the one you reach for and the settings are the one you set
    // once. A Grid rather than a Row so a vertical bar stacks them without a
    // second layout to keep in step.
    Grid {
        id: buttons

        anchors.centerIn: parent
        rows: root.vertical ? 2 : 1
        columns: root.vertical ? 1 : 2

        BarIconButton {
            id: keyboardButton

            bar: root.bar
            text: "\uf11c"
            tooltipText: root.keyboardShown ? "Hide the keyboard" : "Show the keyboard"
            active: root.keyboardShown
            onPressed: function (b) {
                root.toggleKeyboard();
            }
        }

        BarIconButton {
            id: button

            bar: root.bar
            tooltipText: "Gimbal"
            active: root.opened
            onPressed: function (b) {
                root.toggle();
            }

            // A gimbal, drawn rather than borrowed: three rings on three axes,
            // which is the thing the plugin is named after and the thing it
            // does. No Nerd Font glyph is a gimbal, and a tablet outline said
            // less than this does. Drawn as arcs so it scales with the bar and
            // takes the bar's own colour, including when the panel is open.
            iconComponent: Component {
                Item {
                    id: gimbal

                    readonly property real r: Math.min(width, height) * 0.42
                    readonly property real cx: width / 2
                    readonly property real cy: height / 2
                    readonly property color ink: button.active && button.useActiveColor ? button.activeColor : button.foreground

                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer

                        ShapePath {
                            strokeColor: gimbal.ink
                            strokeWidth: Math.max(1, gimbal.r * 0.17)
                            fillColor: "transparent"

                            PathAngleArc {
                                centerX: gimbal.cx
                                centerY: gimbal.cy
                                radiusX: gimbal.r
                                radiusY: gimbal.r
                                startAngle: 0
                                sweepAngle: 360
                            }
                        }

                        ShapePath {
                            strokeColor: gimbal.ink
                            strokeWidth: Math.max(1, gimbal.r * 0.15)
                            fillColor: "transparent"

                            PathAngleArc {
                                centerX: gimbal.cx
                                centerY: gimbal.cy
                                radiusX: gimbal.r * 0.44
                                radiusY: gimbal.r * 0.92
                                startAngle: 0
                                sweepAngle: 360
                            }
                        }

                        ShapePath {
                            strokeColor: gimbal.ink
                            strokeWidth: Math.max(1, gimbal.r * 0.15)
                            fillColor: "transparent"

                            PathAngleArc {
                                centerX: gimbal.cx
                                centerY: gimbal.cy
                                radiusX: gimbal.r * 0.92
                                radiusY: gimbal.r * 0.44
                                startAngle: 0
                                sweepAngle: 360
                            }
                        }
                    }

                    // The load at the centre -- the part all three rings are
                    // there to keep level.
                    Rectangle {
                        anchors.centerIn: parent
                        width: gimbal.r * 0.34
                        height: width
                        radius: width / 2
                        color: gimbal.ink
                    }
                }
            }
        }
    }

    // Panel.qml exposes toggle() for the SUPER+K keybind; the shell will call
    // it in-process for us, so tapping the bar costs no subprocess and takes
    // the same path a keybind does.
    function toggleKeyboard() {
        var shell = root.bar ? root.bar.shell : null;
        if (!shell || typeof shell.callIfLoaded !== "function")
            return;
        shell.callIfLoaded(root.moduleName, "toggle", "");
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
                        text: "Framework 12 tablet mode settings"
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

                Row {
                    width: parent.width
                    spacing: Style.space(6)

                    // Three boxes, each its own switch, because they are not
                    // alternatives: you can want the pads without the edges,
                    // or nothing at all. Colour carries the state rather than
                    // a tick or a shade of grey -- at arm's length on a tablet
                    // that is the difference you can read without looking
                    // twice, and it is the same answer for all three.
                    Repeater {
                        model: [
                            {
                                key: "edges",
                                label: "Edge"
                            },
                            {
                                key: "pads",
                                label: "Pads"
                            }
                        ]

                        Rectangle {
                            id: box

                            required property var modelData
                            readonly property bool on: root.onOff(box.modelData.key)

                            width: (parent.width - Style.space(6)) / 2
                            height: Style.space(34)
                            radius: Style.cornerRadius
                            color: box.on ? root.sage : root.offRed
                            opacity: tap.pressed ? 0.75 : 1.0

                            Behavior on color {
                                ColorAnimation {
                                    duration: 120
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                width: parent.width - Style.space(8)
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: box.modelData.label
                                color: "#1B1B1B"
                                font.family: root.fontFamily
                                font.pixelSize: Style.font.caption
                            }

                            TapHandler {
                                id: tap

                                onTapped: root.setValue(box.modelData.key, !box.on)
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Green is on. The edges take space out of the window area; the pads take none. A pad also shows and hides the keyboard: one tap for that, three to unlock it for moving."
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
                    enabled: root.onOff("edges")
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
