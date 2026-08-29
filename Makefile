# solveig-gtk -- a GTK4 window for Solum, loaded at run time.
#
#   make                build build/gtk.so
#   make run            build it and run examples/counter.sol
#   make clean
#
# This is an *extension*, so it is not part of Solveig and does not build with
# it. That separation is the point rather than an inconvenience: Solveig's front
# page says "no dependencies beyond a C11 compiler and make", and it stays true
# because GTK lives here. Nothing in Solveig's CI needs GTK to be installed.

SOLVEIG ?= ../Solveig

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -g -fPIC
BUILD    = build

# `-std=c11` asks for ISO C and nothing besides, which hides `setenv` and
# friends on glibc. Solveig's Makefile carries the same two lines for the same
# reason; an extension is a C file like any other.
ifeq ($(shell uname -s),Darwin)
STANDARD = -D_DARWIN_C_SOURCE
# A bundle leaves `sol_*` unresolved for solvm to satisfy. ELF does that by
# default; Mach-O has to be told, and refuses to link otherwise.
BUNDLE_LD = -Wl,-undefined,dynamic_lookup
else
STANDARD = -D_XOPEN_SOURCE=700
BUNDLE_LD =
endif

# `-isystem` rather than `-I`, which is what pkg-config hands back.
#
# GTK's headers are not clean under `-Wpedantic` -- G_DECLARE_FINAL_TYPE ends
# with a semicolon outside a function, and every GTK program in the world
# includes it. Under `-I` that is one warning per build, in a file nobody here
# can fix, which trains the eye to skip warnings. Under `-isystem` the compiler
# knows the headers are not ours and stays quiet about them while still warning
# about everything in src/.
GTK_CFLAGS = $(shell pkg-config --cflags gtk4 | sed 's/-I/-isystem /g')
GTK_LIBS   = $(shell pkg-config --libs gtk4)

INCLUDES = -I$(SOLVEIG)/solum/include

TARGET = $(BUILD)/gtk.so

.PHONY: all run clean check

all: $(TARGET)

$(TARGET): src/gtk.c | check
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(STANDARD) $(INCLUDES) $(GTK_CFLAGS) -shared \
	    $< -o $@ $(GTK_LIBS) $(BUNDLE_LD)

# Said once, here, because the two ways this goes wrong both produce compiler
# errors that do not name the cause.
check:
	@pkg-config --exists gtk4 || \
	    { echo "solveig-gtk: gtk4 not found by pkg-config."; \
	      echo "  macOS:  brew install gtk4"; \
	      echo "  Debian: apt install libgtk-4-dev"; exit 1; }
	@test -f $(SOLVEIG)/solum/include/solum/extend.h || \
	    { echo "solveig-gtk: no solum/extend.h under $(SOLVEIG)."; \
	      echo "  Set SOLVEIG to a checkout of Solveig 0.36.0 or later:"; \
	      echo "      make SOLVEIG=/path/to/Solveig"; exit 1; }

run: all
	$(SOLVEIG)/bin/solis --extension=$(TARGET) examples/counter.sol

clean:
	rm -rf $(BUILD)
