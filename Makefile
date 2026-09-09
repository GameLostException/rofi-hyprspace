CC      = gcc
PLUGIN  = rofi-hyprspace.so
SRC     = rofi-hyprspace.c

CFLAGS  = -O2 -fPIC -Wall -Wextra \
          $(shell pkg-config --cflags rofi glib-2.0 cairo)
LDFLAGS = -shared \
          $(shell pkg-config --libs glib-2.0 cairo)

# Install path — rofi looks for plugins here
PLUGINDIR ?= $(HOME)/.local/lib/rofi

.PHONY: all install clean

all: $(PLUGIN)

$(PLUGIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: $(PLUGIN)
	install -d $(PLUGINDIR)
	install -m 755 $(PLUGIN) $(PLUGINDIR)/$(PLUGIN)
	@echo "Installed to $(PLUGINDIR)/$(PLUGIN)"
	@echo "Run rofi with: rofi -show hyprspace -plugin-path $(PLUGINDIR)"

clean:
	rm -f $(PLUGIN)

## Install wrapper script to ~/.local/bin/
install-wrapper:
	install -m 755 rofi-focus-fix.sh $(HOME)/.local/bin/rofi-focus-fix.sh
	@echo "Installed rofi-focus-fix.sh to $(HOME)/.local/bin/"
