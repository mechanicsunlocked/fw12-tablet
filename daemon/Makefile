PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/bin

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic \
           -Wno-unused-parameter -Wshadow -Wstrict-prototypes
LDFLAGS ?=

OBJ = fw12d.o tabletsw.o

all: fw12d

fw12d: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

fw12d.o:    fw12d.c tabletsw.h log.h
tabletsw.o: tabletsw.c tabletsw.h log.h

install: fw12d
	install -Dm755 fw12d $(DESTDIR)$(BINDIR)/fw12d

clean:
	rm -f fw12d $(OBJ)

.PHONY: all install clean
