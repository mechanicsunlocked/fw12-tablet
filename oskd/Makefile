PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/bin

CC      ?= cc
SCANNER ?= wayland-scanner
PROTO   ?= ../proto

CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic \
           -Wno-unused-parameter -Wshadow -Wstrict-prototypes -I.
PKGS     = libsystemd wayland-client xkbcommon
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LIBS     = $(shell pkg-config --libs $(PKGS))

# Generated from the vendored protocol XML. wayland-scanner ships with
# wayland; the XML is vendored because virtual-keyboard-unstable-v1 lives in
# wlr-protocols, which is not packaged on Arch.
GEN_H = virtual-keyboard-unstable-v1-client-protocol.h
GEN_C = virtual-keyboard-unstable-v1-protocol.c

OBJ = oskd.o fcitx.o vkbd.o hypr.o hyprevt.o ipc.o keys.o $(GEN_C:.c=.o)

all: fw12-oskd

$(GEN_H): $(PROTO)/virtual-keyboard-unstable-v1.xml
	$(SCANNER) client-header $< $@

$(GEN_C): $(PROTO)/virtual-keyboard-unstable-v1.xml
	$(SCANNER) private-code $< $@

fw12-oskd: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

oskd.o:  oskd.c  fcitx.h vkbd.h hypr.h hyprevt.h ipc.h keys.h log.h $(GEN_H)
fcitx.o: fcitx.c fcitx.h log.h
vkbd.o:  vkbd.c  vkbd.h  log.h $(GEN_H)
hypr.o:  hypr.c  hypr.h  log.h
hyprevt.o: hyprevt.c hyprevt.h log.h
ipc.o:     ipc.c     ipc.h     log.h
keys.o:    keys.c    keys.h    vkbd.h

install: fw12-oskd
	install -Dm755 fw12-oskd $(DESTDIR)$(BINDIR)/fw12-oskd

clean:
	rm -f fw12-oskd $(OBJ) $(GEN_H) $(GEN_C)

.PHONY: all install clean
