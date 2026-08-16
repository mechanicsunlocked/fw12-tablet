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
        color: dismissArea.pressed
             ? Color.muted
             : Qt.rgba(Color.foreground.r, Color.foreground.g,
                       Color.foreground.b, 0.10)

        Text {
          anchors.centerIn: parent
          text: "" // Nerd Font: chevron down
          color: Color.foreground
          font.pixelSize: Math.round(dismiss.height * 0.5)
        }

        MouseArea {
          id: dismissArea
          anchors.fill: parent
          onPressed: if (root.service) root.service.toggleKeyboard()
        }
      }

      Column {
        id: grid
        anchors.fill: parent
        anchors.margins: Style.space(6)
        spacing: Style.space(4)

        readonly property real rowHeight:
          (height - (spacing * (root.rows.length - 1))) / Math.max(1, root.rows.length)
        readonly property real unit:
          (width - Style.space(4) * (root.rowUnits / 4 - 1)) / root.rowUnits

        Repeater {
          model: root.rows

          Row {
            required property var modelData
            spacing: Style.space(4)
            height: grid.rowHeight
            width: grid.width

            Repeater {
              model: parent.modelData

              Rectangle {
                id: key
                required property var modelData

                readonly property bool isMod: modelData.type === 1
                readonly property bool active:
                  isMod && (root.mods & modelData.mod) !== 0

                width: grid.unit * modelData.w + Style.space(4) * (modelData.w / 4 - 1)
                height: grid.rowHeight
                radius: Style.space(6)

                color: active ? Color.accent
                     : pressArea.pressed ? Color.muted
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

                MouseArea {
                  id: pressArea
                  anchors.fill: parent
                  onPressed: {
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
                }
              }
            }
          }
        }
      }
    }
  }
}
