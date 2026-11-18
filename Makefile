# tspi-usbmon - USB secondary display for LCKFB Taishan Pi (RK3566)
# SPDX-License-Identifier: MIT

CROSS_COMPILE ?=
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
HOSTCC ?= cc
INSTALL ?= install
PREFIX ?= /usr/local
DESTDIR ?=

CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -Isrc
LDFLAGS ?=
ifneq ($(SYSROOT),)
CFLAGS += --sysroot=$(SYSROOT)
LDFLAGS += --sysroot=$(SYSROOT)
endif
LDLIBS := -lturbojpeg -ldrm -lpthread -lm
# Optional Rockchip hardware rotation/scaling; retain CPU fallback.
ifeq ($(WITH_RGA),1)
CFLAGS += -DUM_USE_RGA $(RGA_CFLAGS)
LDLIBS += -lrga
endif

ifeq ($(WITH_MPP),1)
CFLAGS += -DUM_USE_MPP $(MPP_CFLAGS)
LDLIBS += -lrockchip_mpp
endif

SRCS := src/main.c src/gadget.c src/display.c src/jpegdec.c src/render.c src/touch.c \
        src/geom.c src/proto.c src/hid.c src/cfg.c src/log.c
OBJS := $(SRCS:src/%.c=build/%.o)
BIN := build/tspi-usbmon

PURE_SRCS := src/geom.c src/proto.c src/hid.c src/cfg.c src/log.c

.PHONY: all clean test stubs-check install uninstall

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

build/%.o: src/%.c src/usbmon.h src/gadget.h src/display.h src/jpegdec.h src/render.h src/touch.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Host-side unit tests for the dependency-free modules.
test: build/test_pure build/test_render
	./build/test_pure
	./build/test_render

build/test_render: tests/test_render.c src/render.c src/log.c src/render.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=gnu11 -O1 -Wall -Wextra -Isrc -o $@ tests/test_render.c src/render.c src/log.c -lpthread

build/test_pure: tests/test_pure.c $(PURE_SRCS) src/usbmon.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=gnu11 -O1 -Wall -Wextra -Isrc -o $@ tests/test_pure.c $(PURE_SRCS)

# Compile everything against the throw-away stub headers in tests/stubs.
# Useful on a machine without libdrm/turbojpeg development files; never used
# for a real build.
stubs-check:
	@mkdir -p build/stubs
	@for f in $(SRCS); do \
		echo "  CC $$f"; \
		$(HOSTCC) -std=gnu11 -O1 -Wall -Wextra -Itests/stubs -Isrc -c $$f \
			-o build/stubs/$$(basename $$f .c).o || exit 1; \
	done

install: all
	$(INSTALL) -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/tspi-usbmon
	$(INSTALL) -Dm644 deploy/tspi-usbmon.service $(DESTDIR)/etc/systemd/system/tspi-usbmon.service
	$(INSTALL) -Dm644 assets/splash.jpg $(DESTDIR)$(PREFIX)/share/tspi-usbmon/splash.jpg
	@if [ -f $(DESTDIR)/etc/tspi-usbmon.conf ]; then \
		echo "keep existing /etc/tspi-usbmon.conf"; \
	else \
		$(INSTALL) -Dm644 deploy/tspi-usbmon.conf $(DESTDIR)/etc/tspi-usbmon.conf; \
	fi

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tspi-usbmon
	rm -f $(DESTDIR)/etc/systemd/system/tspi-usbmon.service
	rm -rf $(DESTDIR)$(PREFIX)/share/tspi-usbmon

clean:
	rm -rf build
