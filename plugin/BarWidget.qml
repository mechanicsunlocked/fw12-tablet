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

  // Nerd Font: keyboard, and a slashed keyboard when the daemon is down.
  //
  // Written as escapes, which is what the rest of the shell does and now
  // clearly why: the literal glyphs did not survive being written to the file
  // and became empty strings, so this button was invisible on the bar for the
  // whole time it was installed. An escape either renders or shows a box.
  text: daemonUp ? "" : ""
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
