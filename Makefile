# SPDX-License-Identifier: GPL-2.0-or-later

VERSION    := 0.1.0

PREFIX     ?= /usr/local
BINDIR     ?= $(PREFIX)/bin
MANDIR     ?= $(PREFIX)/share/man
UDEVDIR    ?= /usr/lib/udev/rules.d
UNITDIR    ?= /usr/lib/systemd/user
DESTDIR    ?=

CC         ?= cc
CFLAGS     ?= -O2 -g
# override so the project's own flags survive a CFLAGS given on the command line
override CFLAGS   += -std=c11 -Wall -Wextra -Wpedantic -Wshadow \
                     -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-parameter
override CPPFLAGS += -Iinclude -D_GNU_SOURCE -DODYSSEY_VERSION=\"$(VERSION)\"

BIN        := odyssey-ctl
SRCS       := $(wildcard src/*.c)
OBJS       := $(SRCS:.c=.o)
DEPS       := $(OBJS:.o=.d)

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 0644 man/$(BIN).1 $(DESTDIR)$(MANDIR)/man1/$(BIN).1
	install -d $(DESTDIR)$(UDEVDIR)
	install -m 0644 udev/60-odyssey-ctl.rules $(DESTDIR)$(UDEVDIR)/60-odyssey-ctl.rules
	install -m 0644 udev/61-odyssey-ddc-module.rules $(DESTDIR)$(UDEVDIR)/61-odyssey-ddc-module.rules
	install -d $(DESTDIR)$(UNITDIR)
	install -m 0644 systemd/odyssey-ctl.service $(DESTDIR)$(UNITDIR)/odyssey-ctl.service

uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/$(BIN)
	$(RM) $(DESTDIR)$(MANDIR)/man1/$(BIN).1
	$(RM) $(DESTDIR)$(UDEVDIR)/60-odyssey-ctl.rules
	$(RM) $(DESTDIR)$(UDEVDIR)/61-odyssey-ddc-module.rules
	$(RM) $(DESTDIR)$(UNITDIR)/odyssey-ctl.service

module:
	$(MAKE) -C kernel

module-clean:
	$(MAKE) -C kernel clean

check-format:
	clang-format --dry-run --Werror $(SRCS) $(wildcard include/*.h) kernel/odyssey_ddc.c

format:
	clang-format -i $(SRCS) $(wildcard include/*.h) kernel/odyssey_ddc.c

clean:
	$(RM) $(BIN) $(OBJS) $(DEPS)

.PHONY: all install uninstall module module-clean check-format format clean
