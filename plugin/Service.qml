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
  readonly property bool connected: link.item ? link.item.connected : false

  // Latched modifiers, as VKBD_* bits. `locked` survives a keypress
  // (CapsLock); `latched` is one-shot and clears after the next key, which is
  // how every phone keyboard behaves.
  property int latched: 0
  property int locked: 0
  readonly property int mods: latched | locked

  readonly property string socketPath:
    (Quickshell.env("XDG_RUNTIME_DIR") || "/tmp") + "/fw12-oskd.sock"

  signal keymapChanged()

  // Both ends can fail quietly -- a daemon that is not running looks exactly
  // like one that is running but has nothing to say. One line per transition
  // makes the difference visible in the shell log without adding noise.
  onConnectedChanged: console.log("[fw12] daemon", root.connected ? "connected" : "gone")

  function sendKey(code) {
    if (!root.connected) return
    link.item.write(JSON.stringify({ t: "key", code: code, mods: root.mods }) + "\n")
    link.item.flush()
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

  // The daemon may start after the shell, be restarted under it, or be updated
  // by a package upgrade while the shell keeps running. None of those should
  // need a shell restart to recover from, so the connection is a disposable
  // object we rebuild rather than a fixed one we try to revive.
  //
  // Poking the existing Socket -- clearing `path`, re-assigning `connected` --
  // does not work. Measured: after the daemon was killed and restarted, a
  // 2 s retry doing exactly that never reconnected, because a Socket that has
  // already failed keeps that state internally and setting the same properties
  // again is a no-op it never acts on. Destroying the Loader's contents and
  // building a fresh Socket is the only thing that reliably reconnects.
  Loader {
    id: link
    active: true
    sourceComponent: Component {
      Socket {
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
    }
  }

  // Deliberately ungated: the tick runs whether or not we believe we are
  // connected, and decides inside. Driving `running` off the connection state
  // looked tidier and silently stopped retrying for good once the shell's idea
  // of that state stopped matching reality. A 2 s no-op tick costs nothing.
  Timer {
    interval: 2000
    running: true
    repeat: true
    onTriggered: {
      if (root.connected) return
      link.active = false
      link.active = true
    }
  }
}
