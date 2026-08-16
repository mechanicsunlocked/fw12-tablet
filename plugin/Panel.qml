// A draggable button that shows and hides squeekboard.
//
// Why a button and not automatic pop-up: fcitx5 holds Hyprland's single
// input-method-v2 slot (measured -- Hyprland answers a second client with
// `unavailable`, see FINDINGS.md 8.1), so no on-screen keyboard here can see
// which text field has focus. Something has to decide when to show it, and a
// button the user moves where they want is the version with nothing in it to
// go wrong. fcitx5 is not touched, stopped, or reconfigured.
//
// The keyboard itself is squeekboard from `extra`. It types by emitting
// ordinary key events, so fcitx5 still sees everything typed on it and compose
// sequences keep working.

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
    readonly property string modePath: runtimeDir + "/fw12-tablet-mode"
    readonly property string posPath: home + "/.local/state/omarchy/fw12-osk-button.json"

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
    // Keyboard state
    // -----------------------------------------------------------------------
    property bool keyboardShown: false

    // `wanted` is what the keyboard should be doing; `inFlight` is what we are
    // in the middle of telling it. Keeping them apart means a second tap
    // during the round trip is honoured rather than dropped.
    property int wanted: 0 // 1 show, 0 hide
    property int inFlight: -1 // -1 when idle

    function requestKeyboard(on) {
        root.keyboardShown = on;
        root.wanted = on ? 1 : 0;
        if (root.inFlight < 0)
            root.step();
    }

    function step() {
        root.inFlight = root.wanted;
        ensureProc.running = true;
    }

    function finish() {
        var done = root.inFlight;
        root.inFlight = -1;
        // A tap landed while the last one was still being applied.
        if (root.wanted !== done)
            root.step();
    }

    // Every change goes through `systemctl start` first. On a unit that is
    // already up that returns immediately, and because squeekboard's unit is
    // Type=dbus systemd does not return until sm.puri.OSK0 is actually on the
    // bus -- which is exactly the ordering the busctl call below needs. No
    // sleeps and no polling: the dependency is expressed, not waited out.
    Process {
        id: ensureProc

        command: ["systemctl", "--user", "start", "mobi.phosh.OSK"]

        onExited: function (exitCode) {
            if (exitCode !== 0) {
                console.warn("fw12-tablet: could not start squeekboard (mobi.phosh.OSK), exit " + exitCode);
                root.finish();
                return;
            }
            visibleProc.command = ["busctl", "call", "--user", "sm.puri.OSK0", "/sm/puri/OSK0", "sm.puri.OSK0", "SetVisible", "b", root.inFlight === 1 ? "true" : "false"];
            visibleProc.running = true;
        }
    }

    Process {
        id: visibleProc

        onExited: function (exitCode) {
            if (exitCode !== 0)
                console.warn("fw12-tablet: SetVisible failed, exit " + exitCode);
            root.finish();
        }
    }

    // Unfolding back into a laptop puts the keyboard away. Leaving it up would
    // strand a 200 px exclusive zone at the bottom of the screen with no
    // button left on screen to dismiss it.
    onShowButtonChanged: {
        if (showButton)
            ensureProc.running = true;
        else if (keyboardShown)
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
            exclusionMode: ExclusionMode.Ignore

            mask: Region {
                item: button
            }

            readonly property real travelX: Math.max(0, surface.width - root.buttonSize - root.edgeMargin * 2)
            readonly property real travelY: Math.max(0, surface.height - root.buttonSize - root.edgeMargin * 2)

            Item {
                id: button

                width: root.buttonSize
                height: root.buttonSize

                x: root.edgeMargin + surface.travelX * root.fx
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
