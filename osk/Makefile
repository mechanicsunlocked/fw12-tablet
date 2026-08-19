# fw12-oskbd -- the on-screen keyboard.
#
# Originally written for github.com/mechanicsunlocked/fw12tab and vendored here
# (same author, same MIT licence) because it is the right keyboard for this job
# and there is no second one worth having:
#
#   * it uploads the system's own xkb keymap, so Hyprland matches keybinds by
#     keycode exactly as it does for the built-in keyboard, with no
#     `resolve_binds_by_sym` needed and no second copy of the layout to keep in
#     step;
#   * that also means AltGr and dead keys behave as they do on the real board,
#     which is what makes an international layout work at all;
#   * key legends are read out of the keymap at runtime, so changing
#     `input:kb_layout` changes the on-screen keyboard too.
#
# Default install is rootless, into ~/.local. `make PREFIX=/usr install` as
# root puts it in the usual place instead.

PREFIX     ?= $(HOME)/.local
BINDIR     ?= $(PREFIX)/bin
DATADIR    ?= $(PREFIX)/share/fw12-tablet

PKGS        = gtk4 gtk4-layer-shell-0 xkbcommon wayland-client
CFLAGS     ?= -O2 -Wall -Wextra
# The key table sets its trailing runtime fields by omission, on purpose, in
# every one of its ~90 rows. -Wextra objects to each one, and those eighty
# lines of noise are where a real "arm_watchdog defined but not used" sat
# unread until a build from a clean clone put it at the bottom of the output.
# Turning off the one warning that cannot ever be true here is what makes the
# rest of them worth reading.
CFLAGS     += -Wno-missing-field-initializers
CFLAGS     += $(shell pkg-config --cflags $(PKGS))
LDLIBS      = $(shell pkg-config --libs $(PKGS)) -lm

PROTO_XML   = virtual-keyboard-unstable-v1.xml
PROTO_H     = virtual-keyboard-unstable-v1-client-protocol.h
PROTO_C     = virtual-keyboard-unstable-v1-protocol.c

all: fw12-oskbd

# The header has to exist before oskbd.c is compiled, not merely be listed as a
# prerequisite of the link -- it is #included.
$(PROTO_H): $(PROTO_XML)
	wayland-scanner client-header $< $@

$(PROTO_C): $(PROTO_XML)
	wayland-scanner private-code $< $@

fw12-oskbd: oskbd.c $(PROTO_C) $(PROTO_H)
	$(CC) $(CFLAGS) -I. -o $@ oskbd.c $(PROTO_C) $(LDLIBS)

install: fw12-oskbd
	install -Dm755 fw12-oskbd $(DESTDIR)$(BINDIR)/fw12-oskbd
	install -Dm644 framework-logo.svg $(DESTDIR)$(DATADIR)/framework-logo.svg

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/fw12-oskbd
	rm -f $(DESTDIR)$(DATADIR)/framework-logo.svg

clean:
	rm -f fw12-oskbd $(PROTO_C) $(PROTO_H)

.PHONY: all install uninstall clean
