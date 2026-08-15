import QtQuick
import Quickshell
import qs.Commons
import qs.Ui

// Bar indicator: shows whether the keyboard daemon is up, and toggles the
// keyboard by hand for the cases auto-show cannot cover -- a terminal you want
// to type into before focusing anything, or an app with no text-input support.
WidgetButton {
  id: root

  readonly property var service: bar?.shell?.serviceFor("drotiesel.fw12-tablet") ?? null

  readonly property bool daemonUp: service ? service.connected : false
  readonly property bool kbShown: service ? service.shown : false

  // Nerd Font glyphs: keyboard, and a slashed keyboard when the daemon is down.
  text: daemonUp ? "" : ""
  active: kbShown
  dimmed: !daemonUp

  tooltipText: {
    if (!daemonUp) return "fw12-oskd is not running"
    var l = service.layout + (service.variant ? " (" + service.variant + ")" : "")
    return (kbShown ? "Hide keyboard" : "Show keyboard") + " — layout " + l
  }

  onPressed: function (button) {
    if (service) service.toggleKeyboard()
  }
}
