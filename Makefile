PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
CC      ?= clang
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
LDLIBS   = -lmpdclient

cmpc: cmpc.c config.h $(wildcard src/*.inc.c)
	$(CC) $(CFLAGS) -o $@ cmpc.c $(LDLIBS)

static: cmpc.c config.h $(wildcard src/*.inc.c)
	$(CC) $(CFLAGS) -static -o cmpc cmpc.c $(LDLIBS)

install: cmpc
	install -Dm755 cmpc $(DESTDIR)$(BINDIR)/cmpc

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/cmpc

clean:
	rm -rf cmpc build
.PHONY: static clean test test-e2e install uninstall
