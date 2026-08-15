PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/bin

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic \
           -Wno-unused-parameter -Wshadow -Wstrict-prototypes
PKGS     = libsystemd
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LIBS     = $(shell pkg-config --libs $(PKGS))

OBJ = oskd.o fcitx.o

all: fw12-oskd

fw12-oskd: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

oskd.o:  oskd.c  fcitx.h log.h
fcitx.o: fcitx.c fcitx.h log.h

install: fw12-oskd
	install -Dm755 fw12-oskd $(DESTDIR)$(BINDIR)/fw12-oskd

clean:
	rm -f fw12-oskd $(OBJ)

.PHONY: all install clean
