import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.Commons

// The on-screen keyboard surface.
//
// Draws exactly what the daemon sends and nothing more: the daemon owns the
// geometry and the legends, because it is the only thing that knows the xkb
// keymap. A key's label therefore cannot disagree with what it types.
Item {
  id: root

  // Injected by the shell for plugins that pair a panel with a service.
  property var service: null
  property var manifest: null

  readonly property var rows: service ? service.rows : []
  readonly property bool shown: service ? service.shown : false
  readonly property int mods: service ? service.mods : 0

  // A row is 60 quarter-units wide; 4 = one standard key.
  readonly property int rowUnits: 60

  // Which shift level each key should currently show, mirroring what it will
  // actually type given the latched modifiers.
  function levelFor() {
    var shift = (root.mods & 1) !== 0 || (root.mods & 2) !== 0
    var altgr = (root.mods & 128) !== 0
    if (shift && altgr) return 3
    if (altgr) return 2
    if (shift) return 1
    return 0
  }

  PanelWindow {
    id: win
    // `rows.length > 0` is not defensive padding: the daemon sends the keymap
    // on connect, so between mounting and that message there is a window where
    // showing would produce an empty strip that swallows taps.
    visible: root.shown && root.rows.length > 0
    onVisibleChanged: console.log("[fw12] keyboard", visible ? "shown" : "hidden")

    anchors { left: true; right: true; bottom: true }
    implicitHeight: Math.round(screen.height * 0.34)

    WlrLayershell.namespace: "fw12-osk"
    WlrLayershell.layer: WlrLayer.Overlay
    // The whole point: typing must go to the application, never to us.
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    // Reserve the strip so tiled windows sit above the keyboard instead of
    // being covered by it.
    exclusiveZone: visible ? implicitHeight : 0

    color: "transparent"

    Rectangle {
      anchors.fill: parent
      color: Color.background
      opacity: 0.96

      // Put the keyboard down without reaching for the bar. The bar is 6.9 mm
      // tall, which is below every touch-target guideline and hard to hit by
      // hand; this sits on the surface the user is already touching.
      Rectangle {
        id: dismiss
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Style.space(4)
        width: Style.space(28)
        height: Style.space(28)
        radius: width / 2
        z: 1
        // Deliberately higher contrast than a key. This is the only way off
        // the screen, so it has to read as a control at a glance.
        color: dismissArea.pressed
             ? Color.accent
             : Qt.rgba(Color.foreground.r, Color.foreground.g,
                       Color.foreground.b, 0.22)

        // Drawn, not written. The first version used a Nerd Font glyph, which
        // silently became an empty string -- so the button existed, occupied
        // space, and showed nothing. Two rotated bars cannot fail that way and
        // do not depend on which font the theme picked.
        Item {
          anchors.centerIn: parent
          width: Math.round(dismiss.width * 0.42)
          height: width

          Repeater {
            model: [-45, 45]
            Rectangle {
              required property int modelData
              width: parent.width * 0.72
              height: Math.max(2, Math.round(dismiss.height * 0.08))
              radius: height / 2
              color: dismissArea.pressed ? Color.background : Color.foreground
              // Two short bars meeting in the middle: a chevron pointing down,
              // i.e. "put this away".
              x: modelData < 0 ? 0 : parent.width - width
              y: (parent.height - height) / 2
              rotation: modelData
            }
          }
        }

        // TapHandler for the same reason as the keys: a MouseArea here would
        // compete with them for the touch point.
        TapHandler {
          id: dismissArea
          acceptedDevices: PointerDevice.TouchScreen | PointerDevice.Mouse
          onPressedChanged: if (pressed && root.service) root.service.toggleKeyboard()
        }
      }

      // Every pixel belongs to a key.
      //
      // The gaps between keys used to be real: rows and keys were spaced
      // apart, and a touch landing in a gap hit nothing at all. On a screen
      // this size that is a lattice of dead lines across the keyboard, and it
      // reads as "you have to hit the middle of the key".
      //
      // So the spacing is gone and the gap is drawn instead -- each key owns
      // its full share of the row, and the visible rounded rectangle is inset
      // inside it. It looks identical and there is nowhere left to miss.
      Column {
        id: grid
        anchors.fill: parent
        anchors.margins: Style.space(6)
        spacing: 0

        readonly property real gap: Style.space(4)
        readonly property real rowHeight: height / Math.max(1, root.rows.length)

        Repeater {
          model: root.rows

          Row {
            id: keyRow
            required property var modelData
            spacing: 0
            height: grid.rowHeight
            width: grid.width

            // Normalise on the row's own width, not a fixed 60 units. Two rows
            // do not add up to 60 -- the home row is 4 short and the bottom row
            // 5 -- so dividing by 60 left them narrower than the rest, with the
            // space bar smaller than it should be and dead screen on the right.
            // Dividing by what the row actually contains makes every row fill
            // the keyboard, and cannot be wrong for a row added later.
            readonly property real units: {
              var t = 0
              for (var i = 0; i < modelData.length; i++) t += modelData[i].w
              return t > 0 ? t : root.rowUnits
            }

            Repeater {
              model: parent.modelData

              // The touch target: the key's whole share of the row, gap
              // included. Not drawn.
              Item {
                id: key
                required property var modelData

                readonly property bool isMod: modelData.type === 1
                readonly property bool active:
                  isMod && (root.mods & modelData.mod) !== 0

                width: grid.width * modelData.w / keyRow.units
                height: grid.rowHeight

                function fire() {
                  if (!root.service) return
                  if (key.isMod) {
                    // CapsLock is a lock; every other modifier is one-shot,
                    // which is what a touch keyboard wants.
                    if (key.modelData.mod === 2) root.service.toggleLock(key.modelData.mod)
                    else root.service.toggleLatch(key.modelData.mod)
                  } else {
                    root.service.sendKey(key.modelData.code)
                  }
                }

                // The visible key, inset so the gap is painted rather than
                // being a hole in the input surface.
                Rectangle {
                  anchors.fill: parent
                  anchors.margins: grid.gap / 2
                  radius: Style.space(6)

                  color: key.active ? Color.accent
                       : tap.pressed ? Color.muted
                       : Qt.rgba(Color.foreground.r, Color.foreground.g,
                                 Color.foreground.b, 0.10)

                  Behavior on color { ColorAnimation { duration: 90 } }

                  Text {
                    anchors.centerIn: parent
                    text: {
                      var labels = key.modelData.l || []
                      var lv = key.isMod ? 0 : root.levelFor()
                      return labels[lv] && labels[lv].length > 0 ? labels[lv] : (labels[0] || "")
                    }
                    color: key.active ? Color.background : Color.foreground
                    font.pixelSize: Math.max(11, Math.round(grid.rowHeight * 0.38))
                  }
                }

                // TapHandler, not MouseArea. A MouseArea tracks one press at a
                // time, so putting a second finger down before lifting the
                // first meant the second key never fired -- which is exactly
                // what typing fast is. Pointer handlers track touch points
                // independently, so two keys can be down at once.
                TapHandler {
                  id: tap
                  acceptedDevices: PointerDevice.TouchScreen | PointerDevice.Mouse
                  // Fire on touch-down, like a real key, rather than waiting
                  // for the lift to decide it was a tap.
                  onPressedChanged: if (pressed) key.fire()
                }
              }
            }
          }
        }
      }
    }
  }
}
