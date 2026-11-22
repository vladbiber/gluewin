PREFIX ?= /usr/local
CFLAGS ?= -O2
CFLAGS += -std=gnu99 -Wall -Wextra -Wno-missing-field-initializers -Wno-format-truncation $(shell pkg-config --cflags xft)
LDLIBS = $(shell pkg-config --libs xft xrender xext xrandr libpng) -lm

all: gluewin gluewin-settings

gluewin: gluewin.c common.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ gluewin.c $(LDLIBS)

gluewin-settings: gluewin-settings.c common.h
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libjpeg libwebp) $(LDFLAGS) -o $@ gluewin-settings.c $(LDLIBS) $(shell pkg-config --libs libjpeg libwebp)

install: all
	install -Dm755 gluewin $(DESTDIR)$(PREFIX)/bin/gluewin
	install -Dm755 gluewin-settings $(DESTDIR)$(PREFIX)/bin/gluewin-settings
	install -Dm755 gluewin-session $(DESTDIR)$(PREFIX)/bin/gluewin-session
	install -Dm644 gluewin.desktop $(DESTDIR)$(PREFIX)/share/xsessions/gluewin.desktop
	install -Dm644 gluewin-settings.desktop $(DESTDIR)$(PREFIX)/share/applications/gluewin-settings.desktop

clean:
	rm -f gluewin gluewin-settings

.PHONY: all install clean
