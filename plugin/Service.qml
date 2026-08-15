import QtQuick
import Quickshell
import Quickshell.Io

// Shared state for the Framework 12 keyboard.
//
// All the hard parts live in fw12-oskd: it owns the fcitx5 conversation, the
// xkb keymap, and key injection over zwp_virtual_keyboard_v1. This side only
// holds what the UI needs to draw and forwards taps back. Keeping it that way
// is deliberate -- Quickshell is pre-1.0, and rotation and typing must not
// depend on its QML API staying still.
Item {
  id: root

  property var shell: null

  // Populated from the daemon's keymap message.
  property var rows: []
  property string layout: ""
  property string variant: ""
  property bool iso: true

  // fcitx5 asked for the keyboard, i.e. a text field took focus.
  property bool wantVisible: false
  // The user's manual override from the bar button.
  property bool forcedVisible: false
  // NB: not `visible` -- that is FINAL on Item and cannot be overridden.
  readonly property bool shown: forcedVisible || wantVisible
  readonly property bool connected: sock.connected

  // Latched modifiers, as VKBD_* bits. `locked` survives a keypress
  // (CapsLock); `latched` is one-shot and clears after the next key, which is
  // how every phone keyboard behaves.
  property int latched: 0
  property int locked: 0
  readonly property int mods: latched | locked

  readonly property string socketPath:
    (Quickshell.env("XDG_RUNTIME_DIR") || "/tmp") + "/fw12-oskd.sock"

  signal keymapChanged()

  function sendKey(code) {
    if (!sock.connected) return
    sock.write(JSON.stringify({ t: "key", code: code, mods: root.mods }) + "\n")
    sock.flush()
    // One-shot modifiers clear after use; locked ones stay.
    if (root.latched !== 0) root.latched = 0
  }

  function toggleLatch(bit) {
    root.latched = (root.latched & bit) ? (root.latched & ~bit) : (root.latched | bit)
  }

  function toggleLock(bit) {
    root.locked = (root.locked & bit) ? (root.locked & ~bit) : (root.locked | bit)
  }

  function clearMods() {
    root.latched = 0
    root.locked = 0
  }

  function toggleKeyboard() {
    root.forcedVisible = !root.forcedVisible
  }

  function handleLine(line) {
    if (!line) return
    var msg
    try {
      msg = JSON.parse(line)
    } catch (e) {
      return // a partial or malformed line is not worth tearing anything down
    }

    if (msg.t === "keymap") {
      root.layout = msg.layout || ""
      root.variant = msg.variant || ""
      root.iso = msg.iso === true
      root.rows = msg.rows || []
      root.clearMods()
      root.keymapChanged()
    } else if (msg.t === "show") {
      // Cancel any pending hide: fcitx5 emits hide/show pairs constantly as
      // focus moves between surfaces, and acting on each one makes the
      // keyboard flicker.
      hideDelay.stop()
      root.wantVisible = true
    } else if (msg.t === "hide") {
      hideDelay.restart()
    }
  }

  // fcitx5 churns show/hide as focus moves -- a hide immediately followed by a
  // show is the normal case, not the exception. Only a hide that stays
  // unanswered is a real one.
  Timer {
    id: hideDelay
    interval: 300
    onTriggered: {
      root.wantVisible = false
      // Focus really did leave; a latched Shift left behind would otherwise
      // apply to whatever is focused next.
      root.clearMods()
    }
  }

  Socket {
    id: sock
    path: root.socketPath
    connected: true

    parser: SplitParser {
      splitMarker: "\n"
      onRead: line => root.handleLine(line)
    }

    onConnectionStateChanged: {
      if (!connected) {
        // The daemon went away: drop the keyboard rather than leaving a
        // surface that types into nothing.
        root.wantVisible = false
        root.forcedVisible = false
        root.rows = []
      }
    }
  }

  // The daemon may start after the shell, or be restarted under it. Retrying
  // is cheap and means neither has to care about start order.
  //
  // Re-assigning `connected` alone does not retry -- setting it true while the
  // socket is already in a failed state is a no-op, so the shell gave up after
  // one ServerNotFoundError. Clearing `path` tears the socket down so the next
  // assignment builds a fresh one.
  // Deliberately ungated. Driving `running` off `!sock.connected` looked
  // right and silently stopped retrying: after the daemon restarted the shell
  // logged PeerClosedError, then ServerNotFoundError, then nothing. A 2 s
  // no-op tick costs nothing and does not depend on `connected` meaning
  // exactly what we assume.
  Timer {
    interval: 2000
    running: true
    repeat: true
    onTriggered: {
      if (sock.connected) return
      sock.connected = false
      sock.path = ""
      sock.path = root.socketPath
      sock.connected = true
    }
  }
}
