// A draggable button that shows and hides the on-screen keyboard.
//
// Why a button and not automatic pop-up: fcitx5 holds Hyprland's single
// input-method-v2 slot (measured -- Hyprland answers a second client with
// `unavailable`, see FINDINGS.md 8.1), so no on-screen keyboard here can see
// which text field has focus. Something has to decide when to show it, and a
// button the user moves where they want is the version with nothing in it to
// go wrong. fcitx5 is not touched, stopped, or reconfigured.
//
// The keyboard is `fw12-oskbd` (see ../osk/). It uploads the system's own xkb
// keymap, so its keys arrive with the same keycodes as the built-in keyboard's
// and Hyprland matches binds against them without any special configuration --
// SUPER+K from the on-screen Framework key does what it does from the real
// one. It is also why AltGr and dead keys work, which is what an international
// layout needs.
//
// Showing and hiding it is just running and not running it: the binary is
// 38 KB and starts instantly, so there is no daemon to keep alive, no DBus
// call to get wrong, and nothing left behind when the shell exits.

import QtQuick
import QtQuick.Shapes
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons

Item {
    id: root

    // Handed to us by the shell's panel loader when it mounts this plugin.
    property var shell: null
    property var manifest: null

    // -----------------------------------------------------------------------
    // Where the button is allowed to appear
    //
    // Folded state comes from the Lua half of this project, which is what
    // actually sees the SW_TABLET_MODE switch. It writes one word to a file on
    // every transition.
    //
    // An absent file means unknown -- the Lua is not installed, or Hyprland
    // has not reloaded since it was -- and unknown shows the button. Failing
    // visible beats failing invisible: a button you did not expect is obvious
    // and can be dragged out of the way, while a missing one is
    // indistinguishable from a broken plugin.
    // -----------------------------------------------------------------------
    readonly property bool tabletOnly: true

    readonly property string runtimeDir: Quickshell.env("XDG_RUNTIME_DIR") || "/tmp"
    readonly property string home: Quickshell.env("HOME") || ""
    readonly property string modePath: runtimeDir + "/gimbal-mode"
    readonly property string posPath: home + "/.local/state/omarchy/gimbal-button.json"

    property string tabletState: ""
    readonly property bool folded: tabletState !== "laptop"
    readonly property bool showButton: !tabletOnly || folded

    // The internal panel. This is a tablet button; on a docked external
    // monitor it has nothing to do.
    readonly property var targetScreens: {
        var ss = Quickshell.screens;
        for (var i = 0; i < ss.length; i++) {
            if (String(ss[i].name).indexOf("eDP") === 0)
                return [ss[i]];
        }
        return ss.length > 0 ? [ss[0]] : [];
    }

    // -----------------------------------------------------------------------
    // Geometry
    //
    // The position is stored as a fraction of the free travel on each axis
    // rather than in pixels, so it survives the thing this machine does most:
    // rotating. 1200x750 becomes 750x1200, where a pixel position would land
    // off screen or under the bar, while a fraction keeps the button roughly
    // where it looked like it was. It also means x and y stay plain bindings
    // -- nothing has to reposition anything by hand after a rotation.
    // -----------------------------------------------------------------------
    property real fx: 1.0 // 0 = left edge, 1 = right edge
    property real fy: 0.5 // 0 = top edge, 1 = bottom edge

    // 56 logical px is ~12 mm across on this panel, comfortably past the ~9 mm
    // that section 5.4 measured as the point where touch targets start being
    // missed. Style.space() also scales it with the user's text size setting.
    readonly property int buttonSize: Style.space(56)
    readonly property int edgeMargin: Style.space(8)

    function clamp01(v) {
        return Math.max(0, Math.min(1, v));
    }

    // -----------------------------------------------------------------------
    // Keyboard
    //
    // The keyboard is shown exactly when its process is running, so there is
    // one piece of state and it is the true one: no flag that can disagree
    // with reality if the keyboard dies, and nothing to reconcile after a
    // failed call. If it crashes, the button goes dim and the next tap starts
    // it again.
    // -----------------------------------------------------------------------
    readonly property bool keyboardShown: keyboard.running

    function requestKeyboard(on) {
        keyboard.running = on;
    }

    // Reachable from a keybind as
    //   omarchy-shell shell call io.github.mechanicsunlocked.gimbal toggle ''
    // which is what SUPER+K in the Lua half runs. Aiming for a 32 px strip is
    // not always what you want.
    function toggle(arg) {
        var want = !root.keyboardShown;
        root.requestKeyboard(want);
        return want ? "shown" : "hidden";
    }

    // The layout is read from Hyprland rather than configured here, so the
    // on-screen keyboard is whatever the real keyboard is -- change
    // `input:kb_layout` and this follows, with no second copy to update.
    // `hyprctl getoption` answers as two lines, `str: <value>` and `set: ...`.
    property string kbLayout: ""
    property string kbVariant: ""

    function readOption(line, setter) {
        var s = String(line);
        if (s.indexOf("str:") !== 0)
            return;
        // A multi-layout setting like "de,us" starts in the first one.
        setter(s.substring(4).trim().split(",")[0]);
    }

    Process {
        command: ["hyprctl", "getoption", "input:kb_layout"]
        running: true
        stdout: SplitParser {
            onRead: function (line) {
                root.readOption(line, function (v) {
                    root.kbLayout = v;
                });
            }
        }
    }

    Process {
        command: ["hyprctl", "getoption", "input:kb_variant"]
        running: true
        stdout: SplitParser {
            onRead: function (line) {
                root.readOption(line, function (v) {
                    root.kbVariant = v;
                });
            }
        }
    }

    Process {
        id: keyboard

        // Positional, and oskbd wants all three: layout, variant, options.
        command: ["fw12-oskbd", root.kbLayout || "us", root.kbVariant, ""]
        running: false

        onExited: function (exitCode) {
            if (exitCode !== 0)
                console.warn("gimbal: fw12-oskbd exited " + exitCode);
        }
    }

    // Unfolding back into a laptop puts the keyboard away. Leaving it up would
    // strand its exclusive zone at the bottom of the screen with no button
    // left on screen to dismiss it.
    onShowButtonChanged: {
        if (!showButton)
            requestKeyboard(false);
    }

    // -----------------------------------------------------------------------
    // Persistence
    // -----------------------------------------------------------------------
    FileView {
        id: modeFile

        path: root.modePath
        watchChanges: true
        printErrors: false

        // text() is stale inside the change signal itself, so both the first
        // load and every later change are routed through reload -> onLoaded.
        onFileChanged: reload()
        onLoaded: root.tabletState = text().trim()
        onLoadFailed: root.tabletState = ""
    }

    FileView {
        id: posFile

        path: root.posPath
        watchChanges: false
        printErrors: false

        onLoaded: {
            try {
                var p = JSON.parse(text());
                if (typeof p.fx === "number")
                    root.fx = root.clamp01(p.fx);
                if (typeof p.fy === "number")
                    root.fy = root.clamp01(p.fy);
            } catch (e) {}
        }
    }

    // -----------------------------------------------------------------------
    // Swipe actions
    //
    // Read out of this plugin's own entry in ~/.config/omarchy/shell.json,
    // which is where Omarchy keeps per-plugin settings ("the fields on each
    // entry are the values the plugin sees"). Nothing new to learn and nothing
    // extra to install; an absent field falls back to the default below.
    //
    //   { "id": "io.github.mechanicsunlocked.gimbal",
    //     "swipeUp":    "@keyboard",
    //     "swipeDown":  "omarchy-menu",
    //     "swipeRight": "hyprctl dispatch workspace e-1",
    //     "swipeLeft":  "hyprctl dispatch workspace e+1",
    //     "swipeEdge":  16,
    //     "swipeThreshold": 60 }
    //
    // Swipes are named for the direction your finger travels, starting at the
    // matching edge. "@keyboard" is the one built-in action; anything else is
    // run as a command.
    // -----------------------------------------------------------------------
    property var settings: ({})

    readonly property string swipeUp: settings.swipeUp !== undefined ? settings.swipeUp : "@keyboard"
    readonly property string swipeDown: settings.swipeDown !== undefined ? settings.swipeDown : "omarchy-menu"
    // `hyprctl dispatch` takes a *Lua expression* on Omarchy 4, not the
    // hyprland-1 words: `hyprctl dispatch workspace e+1` fails with a Lua
    // syntax error and does nothing. This is the form Omarchy's own workspace
    // widget uses.
    // "-1"/"+1" rather than "e-1"/"e+1": the e- forms only visit workspaces
    // that already have something on them, which makes the gesture skip past
    // empty ones instead of walking the whole set.
    readonly property string swipeRight: settings.swipeRight !== undefined ? settings.swipeRight : "hyprctl dispatch 'hl.dsp.focus({ workspace = \"-1\" })'"
    readonly property string swipeLeft: settings.swipeLeft !== undefined ? settings.swipeLeft : "hyprctl dispatch 'hl.dsp.focus({ workspace = \"+1\" })'"
    readonly property int swipeEdge: settings.swipeEdge !== undefined ? settings.swipeEdge : Style.space(16)
    // The bottom strip is the one you have to find by feel -- when the
    // keyboard is out it is the band between the keys and the window above
    // them -- so it gets twice the depth of the side strips.
    readonly property int swipeEdgeBottom: settings.swipeEdgeBottom !== undefined ? settings.swipeEdgeBottom : Style.space(32)
    // Short on purpose. A strip is 16 px, so the finger is off it almost at
    // once and the rest of the travel is unguided; asking for 60 px made the
    // gesture feel like a drag rather than a flick.
    readonly property int swipeThreshold: settings.swipeThreshold !== undefined ? settings.swipeThreshold : Style.space(30)

    FileView {
        id: settingsFile

        path: root.home + "/.config/omarchy/shell.json"
        watchChanges: true
        printErrors: false

        onFileChanged: reload()
        onLoaded: {
            var found = ({});
            try {
                var list = JSON.parse(text()).plugins || [];
                for (var i = 0; i < list.length; i++) {
                    if (list[i] && list[i].id === "io.github.mechanicsunlocked.gimbal") {
                        found = list[i];
                        break;
                    }
                }
            } catch (e) {}
            root.settings = found;
        }
    }

    function actionFor(key) {
        if (key === "up") return root.swipeUp;
        if (key === "down") return root.swipeDown;
        if (key === "left") return root.swipeLeft;
        return root.swipeRight;
    }

    // `sh -c` because the value is a command line written by a person, and
    // splitting one correctly is the shell's job. One shot per swipe.
    Process {
        id: actionProc

        onExited: function (exitCode) {
            if (exitCode !== 0)
                console.warn("gimbal: swipe command exited " + exitCode + ": " + actionProc.command.join(" "));
        }
    }

    function runAction(key) {
        var cmd = String(root.actionFor(key) || "");
        if (cmd === "")
            return;
        if (cmd === "@keyboard") {
            root.requestKeyboard(!root.keyboardShown);
            return;
        }
        console.log("gimbal: swipe " + key + " -> " + cmd);
        actionProc.running = false;
        actionProc.command = ["sh", "-c", cmd];
        actionProc.running = true;
    }

    // Edge strips. Each is its own small layer surface rather than a masked
    // region of one big one, so that `exclusionMode: Normal` can do the hard
    // part: the compositor places each strip in the area left over by other
    // exclusive zones, which means the bottom strip sits above the keyboard
    // when it is out, and the top strip below the bar -- never on top of
    // either, and with no geometry duplicated here to keep in step.
    // Three strips. The bar owns the real top edge -- a swipe that starts at
    // the top of the screen starts on the bar and the bar gets it -- so there
    // is no top strip, and each remaining strip carries whatever directions it
    // has room for.
    //
    // The menu lives on a downward swipe at either side edge rather than at
    // the bottom: a downward swipe that starts on the bottom strip has 16 px
    // of screen left before the finger runs off the display, which is less
    // than the threshold, so it could never complete. The side strips are full
    // height and have room to spare.
    readonly property var swipeEdges: [
        {
            name: "bottom",
            band: "h",
            aTop: false,
            aBottom: true,
            aLeft: true,
            aRight: true,
            up: "up",
            down: "",
            left: "",
            right: ""
        },
        {
            name: "left",
            band: "v",
            aTop: true,
            aBottom: true,
            aLeft: true,
            aRight: false,
            up: "",
            down: "down",
            left: "",
            right: "right"
        },
        {
            name: "right",
            band: "v",
            aTop: true,
            aBottom: true,
            aLeft: false,
            aRight: true,
            up: "",
            down: "down",
            left: "left",
            right: ""
        }
    ]

    Variants {
        model: root.showButton && root.targetScreens.length > 0 ? root.swipeEdges : []

        PanelWindow {
            id: strip

            required property var modelData
            property bool fired: false

            screen: root.targetScreens[0]
            color: "transparent"

            WlrLayershell.namespace: "fw12-swipe-" + modelData.name
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
            exclusionMode: ExclusionMode.Normal
            exclusiveZone: 0

            anchors {
                top: strip.modelData.aTop
                bottom: strip.modelData.aBottom
                left: strip.modelData.aLeft
                right: strip.modelData.aRight
            }
            implicitWidth: strip.modelData.band === "v" ? root.swipeEdge : 0
            implicitHeight: strip.modelData.band === "h" ? root.swipeEdgeBottom : 0

            DragHandler {
                id: swipe

                target: null

                onActiveChanged: if (swipe.active)
                    strip.fired = false

                onActiveTranslationChanged: {
                    if (!swipe.active || strip.fired)
                        return;
                    var t = swipe.activeTranslation;
                    // Whichever axis the finger committed to decides which of
                    // the strip's four directions this is.
                    var horizontal = Math.abs(t.x) > Math.abs(t.y);
                    var along = horizontal ? t.x : t.y;
                    if (Math.abs(along) < root.swipeThreshold)
                        return;
                    var key = horizontal ? (along < 0 ? strip.modelData.left : strip.modelData.right) : (along < 0 ? strip.modelData.up : strip.modelData.down);
                    if (key === "")
                        return;
                    strip.fired = true;
                    root.runAction(key);
                }
            }
        }
    }

    function savePosition() {
        posFile.setText(JSON.stringify({
            fx: root.fx,
            fy: root.fy
        }));
    }

    // -----------------------------------------------------------------------
    // Window
    //
    // A full-screen transparent layer surface whose input region is masked
    // down to the button alone, so everything else on screen still receives
    // touches normally. Overlay rather than Top: the moment you most want a
    // keyboard is inside a fullscreen Moonlight session, and Top sits below
    // fullscreen windows.
    // -----------------------------------------------------------------------
    Variants {
        model: root.targetScreens

        PanelWindow {
            id: surface

            required property var modelData

            screen: modelData
            visible: root.showButton
            anchors {
                top: true
                bottom: true
                left: true
                right: true
            }
            color: "transparent"

            WlrLayershell.namespace: "fw12-osk-button"
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
            // Reserve nothing, but respect what others reserve: the window is
            // then the area left over by the bar and the keyboard, so the
            // button can be dragged anywhere inside it and still never end up
            // sitting on top of a key or a bar widget. The drag bounds come
            // from the window's own size, so they follow for free.
            exclusionMode: ExclusionMode.Normal
            exclusiveZone: 0

            mask: Region {
                item: button
            }

            // The swipe strips are separate layer surfaces stacked above this
            // one, so wherever the button overlaps one the strip takes the
            // touch and that part of the button is dead -- which is exactly
            // how it ends up feeling stuck once dragged into a corner. Keep
            // its travel inside the space the strips do not claim.
            readonly property int insetSide: root.swipeEdge + root.edgeMargin
            readonly property int insetBottom: root.swipeEdgeBottom + root.edgeMargin
            readonly property real travelX: Math.max(0, surface.width - root.buttonSize - surface.insetSide * 2)
            readonly property real travelY: Math.max(0, surface.height - root.buttonSize - root.edgeMargin - surface.insetBottom)

            Item {
                id: button

                width: root.buttonSize
                height: root.buttonSize

                x: surface.insetSide + surface.travelX * root.fx
                y: root.edgeMargin + surface.travelY * root.fy

                // Left dim while idle so it reads as an accessory rather than
                // something demanding attention, and brought fully up while
                // the keyboard is out so its state is visible at a glance.
                opacity: drag.active ? 1.0 : (root.keyboardShown ? 1.0 : 0.72)
                scale: drag.active ? 1.08 : 1.0

                Behavior on opacity {
                    NumberAnimation {
                        duration: 120
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: 120
                        easing.type: Easing.OutCubic
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    // Round even under the square themes. Everything else the
                    // shell draws is attached to an edge, and takes its shape
                    // from the theme to match its neighbours; this floats over
                    // the middle of whatever you are using with nothing to
                    // match, and a circle is what reads as "grab me and move
                    // me" rather than as a window someone lost.
                    radius: width / 2
                    color: Util.alpha(Color.popups.background, 0.92)
                    border.width: Math.max(1, Style.space(2))
                    border.color: root.keyboardShown ? Color.accent : Util.alpha(Color.popups.border, 0.7)

                    Behavior on border.color {
                        ColorAnimation {
                            duration: 120
                        }
                    }
                }

                // The Framework mark, drawn rather than loaded, so it takes
                // the theme's colour and stays crisp at any size or rotation.
                // The outer cog and the inner circle are one path; the
                // odd-even fill rule is what makes the middle a hole.
                Shape {
                    anchors.centerIn: parent
                    width: root.buttonSize * 0.55
                    height: root.buttonSize * 0.55

                    ShapePath {
                        fillColor: root.keyboardShown ? Color.accent : Color.popups.text
                        strokeWidth: 0
                        strokeColor: "transparent"
                        fillRule: ShapePath.OddEvenFill
                        scale: Qt.size(root.buttonSize * 0.55 / 512, root.buttonSize * 0.55 / 512)

                        PathSvg {
                            path: "m494.6 193.5-37.8-22.4c-17.7-10.5-28.7-30-28.7-51v-45c0-9.2-4.1-17.9-11-23.6-20.6-17.1-43.9-31-69-41-8.4-3.3-17.7-2.7-25.5 1.9L284.7 35a56.05 56.05 0 0 1-57.3 0l-37.9-22.5c-7.7-4.6-17.1-5.3-25.5-1.9-25.2 10-48.3 23.9-68.9 40.9-6.9 5.8-11 14.4-11 23.6V120c0 21-10.9 40.5-28.7 51l-37.8 22.4C9.8 198 4.5 206 3.2 215.1 1 228.4 0 242.1 0 256s1 27.6 3.1 40.9c1.4 9.1 6.7 17.1 14.4 21.7L55.3 341C73.1 351.6 84 371 84 392v44.9c0 9.2 4.1 17.8 11 23.6 20.6 17.1 43.8 31 68.9 40.9 8.4 3.3 17.7 2.7 25.5-1.9l37.9-22.5c17.7-10.5 39.6-10.5 57.3 0l37.9 22.5c7.7 4.6 17.1 5.2 25.5 1.9 25.1-10 48.3-23.9 68.9-40.9 6.9-5.8 11-14.4 11-23.6V392c0-21 11-40.5 28.7-51l37.8-22.4c7.7-4.6 13-12.5 14.4-21.7 2-13.3 3.1-27 3.1-40.9s-1-27.6-3.1-40.9c-1.2-9-6.4-17-14.2-21.6M256.1 414.1c-84.9 0-153.8-70.8-153.8-158 0-87.3 68.9-158 153.8-158s153.8 70.8 153.8 158-68.9 158-153.8 158"
                        }
                    }
                }

                // The handler moves nothing itself; it only reports how far
                // the finger has gone, and that is turned back into the same
                // two fractions the position is stored in. So x and y stay
                // ordinary bindings and a rotation mid-drag cannot desync them.
                DragHandler {
                    id: drag

                    target: null

                    property real startFx: 0
                    property real startFy: 0

                    onActiveChanged: {
                        if (drag.active) {
                            drag.startFx = root.fx;
                            drag.startFy = root.fy;
                        } else {
                            root.savePosition();
                        }
                    }

                    onActiveTranslationChanged: {
                        if (!drag.active)
                            return;
                        if (surface.travelX > 0)
                            root.fx = root.clamp01(drag.startFx + drag.activeTranslation.x / surface.travelX);
                        if (surface.travelY > 0)
                            root.fy = root.clamp01(drag.startFy + drag.activeTranslation.y / surface.travelY);
                    }
                }

                // DragHandler takes an exclusive grab once the finger passes
                // the drag threshold, which cancels this. So a deliberate move
                // never also toggles the keyboard.
                TapHandler {
                    onTapped: root.requestKeyboard(!root.keyboardShown)
                }
            }
        }
    }
}
