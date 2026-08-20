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
    readonly property string oskStatePath: runtimeDir + "/gimbal-osk"
    readonly property string posPath: home + "/.local/state/omarchy/gimbal-button.json"
    readonly property string padPath: home + "/.local/state/omarchy/gimbal-pads.json"
    readonly property string userConfigPath: home + "/.config/omarchy/gimbal.json"

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

    // The two swipe pads, in the same fractions and for the same reason. They
    // start where your thumbs already are when you hold the machine: the two
    // lower corners. Kept as four plain numbers rather than one object per pad
    // because a binding cannot see through a nested property change, and the
    // pads' x and y are bindings.
    property real padLeftFx: 0.0
    property real padLeftFy: 1.0
    property real padRightFx: 1.0
    property real padRightFy: 1.0

    function padFx(id) {
        return id === "left" ? root.padLeftFx : root.padRightFx;
    }
    function padFy(id) {
        return id === "left" ? root.padLeftFy : root.padRightFy;
    }
    function setPadPos(id, fx, fy) {
        if (id === "left") {
            root.padLeftFx = fx;
            root.padLeftFy = fy;
        } else {
            root.padRightFx = fx;
            root.padRightFy = fy;
        }
    }

    // One window per pad per screen. Variants takes a flat model, so the two
    // lists are crossed here rather than nested.
    readonly property var padSurfaces: {
        var out = [];
        var ss = root.targetScreens;
        for (var i = 0; i < ss.length; i++) {
            out.push({
                screen: ss[i],
                pad: "left"
            });
            out.push({
                screen: ss[i],
                pad: "right"
            });
        }
        return out;
    }

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

    // Whether the keyboard is out is also written to a one-byte file, because
    // the bar widget lives in a different plugin instance and a file it can
    // watch is a smaller thing to depend on than the shell's private map of
    // loaded panels. One writer, one reader, no polling.
    onKeyboardShownChanged: oskStateFile.setText(root.keyboardShown ? "1" : "0")

    FileView {
        id: oskStateFile

        path: root.oskStatePath
        printErrors: false

        // A stale "1" from a session that ended with the keyboard out would
        // light the bar button up over nothing, so state is stated once at
        // startup rather than only on change.
        Component.onCompleted: oskStateFile.setText(root.keyboardShown ? "1" : "0")
    }

    // The floating button and the bar button are the same control in two
    // places. Having both is redundant once the bar one exists, so this turns
    // the floating one off without touching tablet mode or the pads.
    readonly property bool floatingButton: root.opt("floatingButton", true) === true

    // -----------------------------------------------------------------------
    // Holding the keyboard back for a game
    //
    // Streaming a desktop game to the tablet, every gesture you make is aimed
    // at the remote machine, and a keyboard sliding up over the picture is
    // never what you meant. So while a Moonlight window is open, nothing
    // summons the keyboard: not a swipe, not the button, not SUPER+B. The
    // gestures themselves keep working, because switching workspace away from
    // the game is exactly what you still want.
    //
    // Detected from the Wayland toplevel list rather than by polling for a
    // process: the list is already live in this process and changes arrive as
    // a signal, so there is no interval to pick and nothing to be stale by.
    // It also asks the right question -- a Moonlight window on screen is what
    // matters, not a Moonlight binary that happens to be resident.
    // -----------------------------------------------------------------------
    readonly property bool blockOnMoonlight: root.opt("blockOnMoonlight", true) === true

    readonly property bool moonlightUp: {
        var m = ToplevelManager.toplevels;
        var vs = m ? m.values : [];
        for (var i = 0; i < vs.length; i++) {
            var id = String(vs[i].appId || "").toLowerCase();
            if (id.indexOf("moonlight") >= 0)
                return true;
        }
        return false;
    }

    readonly property bool keyboardBlocked: root.blockOnMoonlight && root.moonlightUp

    // A game that starts while the keyboard is out takes the screen back.
    onKeyboardBlockedChanged: {
        if (root.keyboardBlocked)
            keyboard.running = false;
    }

    function requestKeyboard(on) {
        if (on && root.keyboardBlocked)
            return;
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

        // Positional: layout, variant, options, and the gutter to keep clear.
        command: ["fw12-oskbd", root.kbLayout || "us", root.kbVariant, "", String(root.swipeGutter)]
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

    FileView {
        id: padFile

        path: root.padPath
        watchChanges: false
        printErrors: false

        onLoaded: {
            try {
                var p = JSON.parse(text());
                if (p.left) {
                    if (typeof p.left.fx === "number")
                        root.padLeftFx = root.clamp01(p.left.fx);
                    if (typeof p.left.fy === "number")
                        root.padLeftFy = root.clamp01(p.left.fy);
                }
                if (p.right) {
                    if (typeof p.right.fx === "number")
                        root.padRightFx = root.clamp01(p.right.fx);
                    if (typeof p.right.fy === "number")
                        root.padRightFy = root.clamp01(p.right.fy);
                }
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
    //     "swipeRight": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"e-1\" })'",
    //     "swipeLeft":  "hyprctl dispatch 'hl.dsp.focus({ workspace = \"e+1\" })'",
    //     "swipeEdge":  16,
    //     "swipeThreshold": 60 }
    //
    // Swipes are named for the direction your finger travels, starting at the
    // matching edge. "@keyboard" is the one built-in action; anything else is
    // run as a command.
    // -----------------------------------------------------------------------
    property var settings: ({})

    // What the bar widget writes. It is a separate file rather than an edit to
    // shell.json because shell.json belongs to Omarchy, and a plugin that
    // rewrites another program's config file will eventually lose a race with
    // it. Values here win over the shell.json entry, which stays usable for
    // anyone who would rather set things by hand.
    property var userSettings: ({})

    function opt(name, fallback) {
        var v = root.userSettings ? root.userSettings[name] : undefined;
        if (v !== undefined && v !== null)
            return v;
        v = root.settings ? root.settings[name] : undefined;
        if (v !== undefined && v !== null)
            return v;
        return fallback;
    }

    FileView {
        id: userConfigFile

        path: root.userConfigPath
        watchChanges: true
        printErrors: false

        onFileChanged: reload()
        onLoaded: {
            try {
                root.userSettings = JSON.parse(text()) || ({});
            } catch (e) {}
        }
        onLoadFailed: root.userSettings = ({})
    }

    // Which of the two mechanisms is live: "edges", "pads" or "both".
    readonly property string mode: String(root.opt("mode", "both"))
    readonly property bool edgesOn: root.mode !== "pads"

    readonly property string swipeUp: root.opt("swipeUp", "@keyboard")
    readonly property string swipeDown: root.opt("swipeDown", "omarchy-menu")
    // `hyprctl dispatch` takes a *Lua expression* on Omarchy 4, not the
    // hyprland-1 words: `hyprctl dispatch workspace e+1` fails with a Lua
    // syntax error and does nothing. This is the form Omarchy's own workspace
    // widget uses.
    // The two directions are deliberately not the same form. Measured on this
    // machine with workspaces 1..4 live:
    //
    //   from 1, focus("-1")  -> 1     a wall
    //   from 4, focus("+1")  -> 5     a new workspace
    //   from 1, focus("e-1") -> 4     wraps to the last one that exists
    //   from 4, focus("e+1") -> 1     wraps to the first
    //
    // Forward is "+1": swiping past the end opens a new workspace, which is
    // the point of swiping past the end. Hyprland drops it again when you
    // leave it empty, so it does not accumulate.
    //
    // Back is "e-1": it wraps to the last workspace that exists rather than
    // stopping dead at 1. "-1" is the only one of the four that can do
    // nothing at all, and a swipe that does nothing reads as a broken gesture
    // -- there is no feedback to tell you that you are simply at the end.
    readonly property string swipeRight: root.opt("swipeRight", "hyprctl dispatch 'hl.dsp.focus({ workspace = \"e-1\" })'")
    readonly property string swipeLeft: root.opt("swipeLeft", "hyprctl dispatch 'hl.dsp.focus({ workspace = \"+1\" })'")
    readonly property int swipeEdge: root.opt("swipeEdge", Style.space(16))
    // The bottom strip is the one you have to find by feel -- when the
    // keyboard is out it is the band between the keys and the window above
    // them -- so it gets twice the depth of the side strips.
    readonly property int swipeEdgeBottom: root.opt("swipeEdgeBottom", Style.space(32))

    // The gutter is the swipe strip that survives the keyboard.
    //
    // The ordinary side strips respect what other surfaces reserve, so the
    // moment the keyboard claims the bottom of the screen they stop at the top
    // of it -- and the whole lower half of the display has nowhere to start a
    // gesture from. The gutters ignore reservations and run the full height
    // instead, and the keyboard is told to keep this much clear on each side
    // (argv[4]) so a gutter is never sitting on top of a key.
    readonly property int swipeGutter: Math.max(0, root.opt("swipeGutter", Style.space(30)))
    // Short on purpose. A strip is 16 px, so the finger is off it almost at
    // once and the rest of the travel is unguided; asking for 60 px made the
    // gesture feel like a drag rather than a flick.
    readonly property int swipeThreshold: root.opt("swipeThreshold", Style.space(30))

    // The two swipe pads. Set from the bar widget's Interaction setting.
    readonly property bool showPads: root.mode !== "edges"

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

    // How far a finger can actually travel before it leaves the display.
    //
    // A strip anchored to an edge is a dead end in that direction: swipe left
    // on the left edge and the glass runs out. The room is *not* the strip's
    // width -- it is the distance from where the finger landed to the screen
    // edge, which on a 30 px strip averages 15 px and can be nearly nothing.
    //
    // Measuring it as the full width was the first attempt and it made every
    // outward swipe impossible: the log showed 19 inward gestures firing and
    // not one outward, because an 18 px threshold cannot be met from an
    // average of 15 px of travel. Since each strip is anchored to the edge it
    // is limited by, the press position within the surface *is* that distance.
    //
    // The threshold is most of whatever room there is, floored so a brush
    // cannot trigger anything -- which also means a finger landing right on
    // the edge correctly gets no gesture, because there is nowhere to go.
    // Directions with the whole screen to play with keep the full threshold.
    function swipeThresholdFor(m, horizontal, along, press, w, h) {
        var room = Infinity;
        if (horizontal) {
            if (along < 0 && m.aLeft)
                room = press.x;
            else if (along > 0 && m.aRight)
                room = w - press.x;
        } else if (along > 0 && m.aBottom) {
            room = h - press.y;
        }
        if (room === Infinity)
            return root.swipeThreshold;
        return Math.max(8, Math.min(root.swipeThreshold, room * 0.6));
    }

    // Which surface the last gesture came from. Only for the log, but the
    // log is how the last two swipe reports were settled, and "it fired" and
    // "it fired *there*" are different facts.
    property string lastSwipeFrom: ""

    function runAction(key) {
        var cmd = String(root.actionFor(key) || "");
        if (cmd === "")
            return;
        if (cmd === "@keyboard") {
            root.requestKeyboard(!root.keyboardShown);
            return;
        }
        console.log("gimbal: swipe " + key + " on " + root.lastSwipeFrom + " -> " + cmd);
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
            down: "down",
            left: "left",
            right: "right"
        },
        {
            name: "left",
            band: "v",
            aTop: true,
            aBottom: true,
            aLeft: true,
            aRight: false,
            up: "up",
            down: "down",
            left: "left",
            right: "right"
        },
        {
            name: "right",
            band: "v",
            aTop: true,
            aBottom: true,
            aLeft: false,
            aRight: true,
            up: "up",
            down: "down",
            left: "left",
            right: "right"
        }
    ]

    Variants {
        model: root.showButton && root.edgesOn && root.targetScreens.length > 0 ? root.swipeEdges : []

        PanelWindow {
            id: strip

            required property var modelData
            property bool fired: false
            // 0..1, how far this gesture has got toward committing. It drives
            // the only feedback a swipe has: without it there is no telling a
            // gesture that fell short of the threshold from one that was never
            // seen at all, and that is most of what makes an edge gesture feel
            // unreliable.
            property real progress: 0
            property point pressAt: Qt.point(0, 0)

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
            implicitWidth: strip.modelData.band === "v" ? root.swipeGutter : 0
            implicitHeight: strip.modelData.band === "h" ? root.swipeEdgeBottom : 0


            // Where the finger landed, and how close it is to committing.
            //
            // A swipe is the one gesture with no natural feedback: a button
            // lights up under a thumb, a swipe just either happens or does
            // not, and when it does not there is nothing to say whether it
            // was too short, in the wrong place, or never seen. This is that
            // missing half -- it appears under the finger the moment a strip
            // takes the touch, so the live area teaches itself, and it fills
            // as the threshold is approached so a gesture that fell short
            // looks different from one that was ignored.
            Rectangle {
                id: pip

                readonly property real span: Math.min(strip.width, strip.height)

                x: strip.pressAt.x - width / 2
                y: strip.pressAt.y - height / 2
                width: pip.span * (0.55 + 0.45 * strip.progress)
                height: width
                radius: width / 2

                color: strip.progress >= 1 ? Color.accent : Util.alpha(Color.popups.text, 0.5)
                opacity: swipe.active ? (0.35 + 0.65 * strip.progress) : 0
                visible: opacity > 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: 130
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on width {
                    NumberAnimation {
                        duration: 60
                    }
                }
                Behavior on color {
                    ColorAnimation {
                        duration: 80
                    }
                }
            }
            DragHandler {
                id: swipe

                target: null

                onActiveChanged: {
                    if (swipe.active) {
                        strip.fired = false;
                        strip.pressAt = swipe.centroid.pressPosition;
                    }
                    strip.progress = 0;
                }

                onActiveTranslationChanged: {
                    if (!swipe.active || strip.fired)
                        return;
                    var t = swipe.activeTranslation;
                    // Whichever axis the finger committed to decides which of
                    // the strip's four directions this is.
                    var horizontal = Math.abs(t.x) > Math.abs(t.y);
                    var along = horizontal ? t.x : t.y;
                    var need = root.swipeThresholdFor(strip.modelData, horizontal, along, swipe.centroid.pressPosition, strip.width, strip.height);
                    strip.progress = Math.min(1, Math.abs(along) / need);
                    if (Math.abs(along) < need)
                        return;
                    var key = horizontal ? (along < 0 ? strip.modelData.left : strip.modelData.right) : (along < 0 ? strip.modelData.up : strip.modelData.down);
                    if (key === "")
                        return;
                    strip.fired = true;
                    root.lastSwipeFrom = strip.modelData.name;
                    root.runAction(key);
                }
            }
        }
    }

    // Two full-height gutters, one per side, carrying the same four gestures
    // as the side strips.
    //
    // ExclusionMode.Ignore is the whole point: these are the surfaces that do
    // not step aside for the keyboard, so a swipe works in the same place
    // whether it is up or not. They only need to reach as far up as the
    // keyboard ever does -- half the screen is comfortably past the 0.45 cap in
    // oskbd's sizing -- and above that the ordinary side strips take over.
    // Where the two overlap they do the same thing, so it does not matter
    // which one gets the finger.
    Variants {
        model: root.showButton && root.edgesOn && root.swipeGutter > 0 && root.targetScreens.length > 0 ? [
            {
                name: "gutter-left",
                band: "v",
                aBottom: true,
                aLeft: true,
                aRight: false,
                up: "up",
                down: "down",
                left: "left",
                right: "right"
            },
            {
                name: "gutter-right",
                band: "v",
                aBottom: true,
                aLeft: false,
                aRight: true,
                up: "up",
                down: "down",
                left: "left",
                right: "right"
            },
            {
                // The strip below the keyboard. The bottom edge is where a
                // thumb goes without looking, and it is the one the keyboard
                // would otherwise take entirely.
                //
                // No downward gesture here and nowhere else to put one: a
                // downward swipe starting on this strip has the strip's own
                // height before the finger leaves the display, which is under
                // the threshold, so it could never complete. The menu stays on
                // the side edges, which are full height.
                name: "gutter-bottom",
                band: "h",
                aBottom: true,
                aLeft: true,
                aRight: true,
                up: "up",
                down: "down",
                left: "left",
                right: "right"
            }
        ] : []

        PanelWindow {
            id: gutter

            required property var modelData
            property bool fired: false
            // 0..1, how far this gesture has got toward committing. It drives
            // the only feedback a swipe has: without it there is no telling a
            // gesture that fell short of the threshold from one that was never
            // seen at all, and that is most of what makes an edge gesture feel
            // unreliable.
            property real progress: 0
            property point pressAt: Qt.point(0, 0)

            screen: root.targetScreens[0]
            color: "transparent"

            WlrLayershell.namespace: "fw12-swipe-" + modelData.name
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
            exclusionMode: ExclusionMode.Ignore
            exclusiveZone: 0

            anchors {
                top: false
                bottom: true
                left: gutter.modelData.aLeft
                right: gutter.modelData.aRight
            }
            // A side gutter only has to reach as far up as the keyboard ever
            // does; half the screen is comfortably past the 0.45 cap in
            // oskbd's sizing, and above that the ordinary side strips take
            // over. The bottom one is just the reserved strip itself.
            implicitWidth: gutter.modelData.band === "v" ? root.swipeGutter : 0
            implicitHeight: gutter.modelData.band === "v" ? (gutter.screen ? gutter.screen.height * 0.5 : 0) : root.swipeGutter

            // Nothing here is visible until the keyboard is, because until then
            // the whole edge already works and a marker would only be clutter.
            // Once the keyboard is up the live area is no longer where anyone
            // would guess, so it says where it is: a thin bar down the middle
            // of the gutter, dim enough to ignore and present enough to find.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                width: gutter.modelData.band === "v" ? Math.max(2, root.swipeGutter * 0.14) : parent.width * 0.28
                height: gutter.modelData.band === "v" ? parent.height * 0.42 : Math.max(2, root.swipeGutter * 0.14)
                radius: Math.min(width, height) / 2
                color: Util.alpha(Color.popups.text, 0.35)
                visible: root.keyboardShown

                Behavior on opacity {
                    NumberAnimation {
                        duration: 150
                    }
                }
            }


            // Where the finger landed, and how close it is to committing.
            //
            // A swipe is the one gesture with no natural feedback: a button
            // lights up under a thumb, a swipe just either happens or does
            // not, and when it does not there is nothing to say whether it
            // was too short, in the wrong place, or never seen. This is that
            // missing half -- it appears under the finger the moment a strip
            // takes the touch, so the live area teaches itself, and it fills
            // as the threshold is approached so a gesture that fell short
            // looks different from one that was ignored.
            Rectangle {
                id: pip

                readonly property real span: Math.min(gutter.width, gutter.height)

                x: gutter.pressAt.x - width / 2
                y: gutter.pressAt.y - height / 2
                width: pip.span * (0.55 + 0.45 * gutter.progress)
                height: width
                radius: width / 2

                color: gutter.progress >= 1 ? Color.accent : Util.alpha(Color.popups.text, 0.5)
                opacity: gutterSwipe.active ? (0.35 + 0.65 * gutter.progress) : 0
                visible: opacity > 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: 130
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on width {
                    NumberAnimation {
                        duration: 60
                    }
                }
                Behavior on color {
                    ColorAnimation {
                        duration: 80
                    }
                }
            }
            DragHandler {
                id: gutterSwipe

                target: null

                onActiveChanged: {
                    if (gutterSwipe.active) {
                        gutter.fired = false;
                        gutter.pressAt = gutterSwipe.centroid.pressPosition;
                    }
                    gutter.progress = 0;
                }

                onActiveTranslationChanged: {
                    if (!gutterSwipe.active || gutter.fired)
                        return;
                    var t = gutterSwipe.activeTranslation;
                    var horizontal = Math.abs(t.x) > Math.abs(t.y);
                    var along = horizontal ? t.x : t.y;
                    var need = root.swipeThresholdFor(gutter.modelData, horizontal, along, gutterSwipe.centroid.pressPosition, gutter.width, gutter.height);
                    gutter.progress = Math.min(1, Math.abs(along) / need);
                    if (Math.abs(along) < need)
                        return;
                    // Upward is the keyboard, on both gutters, so the gesture
                    // that summons it is also the one that dismisses it, from
                    // the same place.
                    var key = horizontal ? (along < 0 ? gutter.modelData.left : gutter.modelData.right) : (along < 0 ? gutter.modelData.up : gutter.modelData.down);
                    if (key === "")
                        return;
                    gutter.fired = true;
                    root.lastSwipeFrom = gutter.modelData.name;
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

    function savePads() {
        padFile.setText(JSON.stringify({
            left: {
                fx: root.padLeftFx,
                fy: root.padLeftFy
            },
            right: {
                fx: root.padRightFx,
                fy: root.padRightFy
            }
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
            visible: root.showButton && root.floatingButton
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
            readonly property int insetSide: Math.max(root.swipeEdge, root.swipeGutter) + root.edgeMargin
            readonly property int insetBottom: root.swipeEdgeBottom + root.edgeMargin
            readonly property real travelX: Math.max(0, surface.width - root.buttonSize - surface.insetSide * 2)
            readonly property real travelY: Math.max(0, surface.height - root.buttonSize - root.edgeMargin - surface.insetBottom)

            Item {
                id: button

                width: root.buttonSize
                height: root.buttonSize

                // 0..1 toward committing a flick, same as the strips use.
                property real progress: 0

                x: surface.insetSide + surface.travelX * root.fx
                y: root.edgeMargin + surface.travelY * root.fy

                // Left dim while idle so it reads as an accessory rather than
                // something demanding attention, and brought fully up while
                // the keyboard is out so its state is visible at a glance.
                opacity: drag.active ? 1.0 : (root.keyboardShown ? 1.0 : 0.72)
                // Picked up, it lifts. Flicked, it does not move at all, so
                // the only thing that can report the gesture is the ring below.
                scale: button.moveMode ? 1.18 : 1.0

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

                // A ring that grows out of the button as a flick commits.
                // The button cannot move to report the gesture -- moving is
                // what holding does -- so this is the only thing that can.
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * (1 + 0.5 * button.progress)
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: Math.max(1, Style.space(2))
                    border.color: button.progress >= 1 ? Color.accent : Util.alpha(Color.popups.text, 0.45)
                    opacity: (drag.active && !button.moveMode) ? button.progress : 0
                    visible: opacity > 0

                    Behavior on opacity {
                        NumberAnimation {
                            duration: 90
                        }
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
                // The button is the one control that is not against an edge,
                // so a swipe from it has the whole screen in every direction.
                // That makes it the only place all four gestures are equally
                // good -- an edge strip can never offer the direction that
                // points off the display, because the glass runs out.
                //
                // Flick it for an action, hold it to pick it up. The hold is
                // what keeps the two apart: without it, moving the button and
                // swiping from it are the same motion, and one of them has to
                // lose. Holding is also the rarer intent -- a button gets
                // moved once and used daily.
                property bool moveMode: false

                Timer {
                    id: holdTimer

                    interval: 350
                    onTriggered: button.moveMode = true
                }

                DragHandler {
                    id: drag

                    target: null

                    property real startFx: 0
                    property real startFy: 0
                    property bool fired: false

                    onActiveChanged: {
                        if (drag.active) {
                            drag.startFx = root.fx;
                            drag.startFy = root.fy;
                            drag.fired = false;
                        } else {
                            if (button.moveMode)
                                root.savePosition();
                            holdTimer.stop();
                            button.moveMode = false;
                            button.progress = 0;
                        }
                    }

                    onActiveTranslationChanged: {
                        if (!drag.active)
                            return;

                        if (button.moveMode) {
                            if (surface.travelX > 0)
                                root.fx = root.clamp01(drag.startFx + drag.activeTranslation.x / surface.travelX);
                            if (surface.travelY > 0)
                                root.fy = root.clamp01(drag.startFy + drag.activeTranslation.y / surface.travelY);
                            return;
                        }

                        if (drag.fired)
                            return;

                        var t = drag.activeTranslation;
                        var horizontal = Math.abs(t.x) > Math.abs(t.y);
                        var along = horizontal ? t.x : t.y;
                        // Moving at all means this is a flick and not a hold,
                        // so the button must not turn into a draggable one
                        // underneath the gesture.
                        if (Math.abs(t.x) > 4 || Math.abs(t.y) > 4)
                            holdTimer.stop();
                        button.progress = Math.min(1, Math.abs(along) / root.swipeThreshold);
                        if (Math.abs(along) < root.swipeThreshold)
                            return;
                        drag.fired = true;
                        root.lastSwipeFrom = "button";
                        root.runAction(horizontal ? (along < 0 ? "left" : "right") : (along < 0 ? "up" : "down"));
                    }
                }

                // DragHandler takes an exclusive grab once the finger passes
                // the drag threshold, which cancels this. So a deliberate move
                // never also toggles the keyboard.
                TapHandler {
                    onPressedChanged: {
                        if (pressed)
                            holdTimer.restart();
                        else
                            holdTimer.stop();
                    }
                    onTapped: root.requestKeyboard(!root.keyboardShown)
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Swipe pads
    //
    // Two round pads that take all four gestures, sitting where your thumbs
    // already are when you hold the machine. They exist because an edge strip
    // is the one place a swipe can never be given room in every direction: at
    // the left edge there is nothing to the left of your finger, so the
    // outward gesture has to fire on a few millimetres of travel or not at
    // all. A pad away from the edge has the whole screen in all four
    // directions, and the same threshold everywhere.
    //
    // Press and drag to fire. There is no hold delay and no flick-versus-move
    // ambiguity to arbitrate, because moving a pad is behind a separate
    // gesture entirely: three taps unlock it, three more stick it down. That
    // is deliberately not something a hand does by accident, and it is why
    // the press-and-drag can start acting immediately.
    //
    // Tap counting is done here rather than left to TapHandler.tapCount, which
    // keys off the platform's double-click interval -- a mouse setting, on a
    // control that is only ever touched.
    // -----------------------------------------------------------------------
    Variants {
        model: root.padSurfaces

        PanelWindow {
            id: padSurface

            required property var modelData

            readonly property string padId: modelData.pad

            screen: modelData.screen
            visible: root.showButton && root.showPads
            anchors {
                top: true
                bottom: true
                left: true
                right: true
            }
            color: "transparent"

            WlrLayershell.namespace: "gimbal-pad-" + padSurface.padId
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
            // Reserve nothing, and ignore what others reserve. A pad is a
            // control you learn the position of with your thumb, so it has to
            // be in the same place every time -- if the window shrank to the
            // space left by the keyboard, every pad would jump the moment the
            // keyboard appeared. The pads sit on the overlay layer and the
            // keyboard on the top layer, so a pad left over the keyboard is
            // drawn above it and still takes the touch; it covers whatever key
            // is under it, which is the reason they can be dragged.
            exclusionMode: ExclusionMode.Ignore
            exclusiveZone: 0

            mask: Region {
                item: pad
            }

            // The swipe strips are stacked above this surface, so any part of
            // a pad overlapping one is dead to the touch. Keep the travel
            // inside what the strips do not claim.
            readonly property int insetSide: Math.max(root.swipeEdge, root.swipeGutter) + root.edgeMargin
            readonly property int insetBottom: root.swipeEdgeBottom + root.edgeMargin
            readonly property real travelX: Math.max(0, padSurface.width - root.buttonSize - padSurface.insetSide * 2)
            readonly property real travelY: Math.max(0, padSurface.height - root.buttonSize - root.edgeMargin - padSurface.insetBottom)

            Item {
                id: pad

                width: root.buttonSize
                height: root.buttonSize

                x: padSurface.insetSide + padSurface.travelX * root.padFx(padSurface.padId)
                y: root.edgeMargin + padSurface.travelY * root.padFy(padSurface.padId)

                // Unlocked by three taps, and stays unlocked until three more.
                property bool loose: false
                // 0..1 toward committing a swipe, same as the strips use.
                property real progress: 0
                property int tapRun: 0

                opacity: padDrag.active ? 1.0 : 0.6
                scale: pad.loose ? 1.18 : 1.0

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

                // A ring that grows as a swipe commits. The pad does not move
                // under the finger -- moving is what the unlocked state does
                // -- so this is the only thing that can report the gesture.
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * (1 + 0.5 * pad.progress)
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: Math.max(1, Style.space(2))
                    border.color: pad.progress >= 1 ? Color.accent : Util.alpha(Color.popups.text, 0.45)
                    opacity: (padDrag.active && !pad.loose) ? pad.progress : 0
                    visible: opacity > 0

                    Behavior on opacity {
                        NumberAnimation {
                            duration: 90
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: Util.alpha(Color.popups.background, 0.82)
                    border.width: Math.max(1, Style.space(2))
                    border.color: pad.loose ? Color.accent : Util.alpha(Color.popups.border, 0.7)

                    Behavior on border.color {
                        ColorAnimation {
                            duration: 120
                        }
                    }
                }

                // Four arrowheads. The pad has to say what it is without a
                // label, because nothing about a plain circle suggests that
                // dragging off it does anything.
                Shape {
                    anchors.centerIn: parent
                    width: root.buttonSize * 0.5
                    height: width

                    ShapePath {
                        fillColor: pad.loose ? Color.accent : Util.alpha(Color.popups.text, 0.8)
                        strokeWidth: 0
                        strokeColor: "transparent"
                        scale: Qt.size(root.buttonSize * 0.5 / 100, root.buttonSize * 0.5 / 100)

                        PathSvg {
                            path: "M50 6 L66 30 L34 30 Z M50 94 L34 70 L66 70 Z M6 50 L30 34 L30 66 Z M94 50 L70 66 L70 34 Z"
                        }
                    }
                }

                // Three taps inside this window unlock or stick the pad. The
                // window restarts on every tap, so it is three taps in a row
                // rather than three within a fixed period.
                Timer {
                    id: tapWindow

                    interval: 450
                    onTriggered: pad.tapRun = 0
                }

                DragHandler {
                    id: padDrag

                    target: null

                    property real startFx: 0
                    property real startFy: 0
                    property bool fired: false

                    onActiveChanged: {
                        if (padDrag.active) {
                            padDrag.startFx = root.padFx(padSurface.padId);
                            padDrag.startFy = root.padFy(padSurface.padId);
                            padDrag.fired = false;
                        } else {
                            if (pad.loose)
                                root.savePads();
                            pad.progress = 0;
                        }
                    }

                    onActiveTranslationChanged: {
                        if (!padDrag.active)
                            return;

                        if (pad.loose) {
                            var nx = padDrag.startFx;
                            var ny = padDrag.startFy;
                            if (padSurface.travelX > 0)
                                nx = root.clamp01(padDrag.startFx + padDrag.activeTranslation.x / padSurface.travelX);
                            if (padSurface.travelY > 0)
                                ny = root.clamp01(padDrag.startFy + padDrag.activeTranslation.y / padSurface.travelY);
                            root.setPadPos(padSurface.padId, nx, ny);
                            return;
                        }

                        if (padDrag.fired)
                            return;

                        var t = padDrag.activeTranslation;
                        var horizontal = Math.abs(t.x) > Math.abs(t.y);
                        var along = horizontal ? t.x : t.y;
                        pad.progress = Math.min(1, Math.abs(along) / root.swipeThreshold);
                        if (Math.abs(along) < root.swipeThreshold)
                            return;
                        padDrag.fired = true;
                        root.lastSwipeFrom = "pad-" + padSurface.padId;
                        root.runAction(horizontal ? (along < 0 ? "left" : "right") : (along < 0 ? "up" : "down"));
                    }
                }

                // A swipe takes an exclusive grab past the drag threshold,
                // which cancels this, so a gesture never counts as a tap.
                TapHandler {
                    onTapped: {
                        pad.tapRun += 1;
                        tapWindow.restart();
                        if (pad.tapRun < 3)
                            return;
                        pad.tapRun = 0;
                        tapWindow.stop();
                        pad.loose = !pad.loose;
                        if (!pad.loose)
                            root.savePads();
                    }
                }
            }
        }
    }

}
