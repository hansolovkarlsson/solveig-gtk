# solveig-gtk -- a GTK4 window for Solum, loaded at run time.
#
#   make                build build/gtk.so
#   make run            build it and run examples/counter.sol
#   make counter        the same, said by name
#   make circles        build it and run examples/circles.sol
#   make edit           build it and run examples/edit.sol
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

# The Solveig this is being built against, read from the same header the
# binaries report their version out of.
#
# Checked rather than assumed because the failure it prevents is unhelpful: an
# older checkout has no solum/extend.h at all, so the compiler says a header is
# missing and says nothing about why. The version says why.
#
# The run-time half is separate and stays separate: SOL_EXTENSION_ABI is
# compared when the bundle loads, and catches a Solveig whose structs moved
# under a bundle that was built earlier. This check is only "does this Solveig
# have an extension interface at all".
# 0.36.0 is where the extension interface arrived and is all the *bundle* needs
# to build. The examples need 0.37.0, because the editor uses `string:replace` --
# which it is the reason for. One number rather than two, set to the higher of
# them, because a checkout that builds the bundle and cannot run `make run` would
# be a worse thing to hand somebody than a version requirement half a release
# stricter than the C strictly requires.
SOLVEIG_MINIMUM = 0.37.0
SOLVEIG_VERSION = $(shell grep SOLUM_VERSION \
                    $(SOLVEIG)/solum/include/solum/common.h 2>/dev/null \
                    | tr -d '"' | awk '{print $$3}')

INCLUDES = -I$(SOLVEIG)/solum/include

TARGET = $(BUILD)/gtk.so

.PHONY: all run counter circles edit clean check

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
	@test -n "$(SOLVEIG_VERSION)" || \
	    { echo "solveig-gtk: $(SOLVEIG) is not a Solveig checkout."; \
	      echo "      make SOLVEIG=/path/to/Solveig"; exit 1; }
	@echo "$(SOLVEIG_VERSION) $(SOLVEIG_MINIMUM)" \
	    | awk '{ split($$1, a, "."); split($$2, b, "."); \
	             exit !(a[1] > b[1] || (a[1] == b[1] && a[2] >= b[2])) }' || \
	    { echo "solveig-gtk: found Solveig $(SOLVEIG_VERSION) under $(SOLVEIG),"; \
	      echo "  and this needs $(SOLVEIG_MINIMUM) or later."; \
	      echo "  Update that checkout, or point SOLVEIG at a newer one."; exit 1; }

run counter: all
	$(SOLVEIG)/bin/solis --extension=$(TARGET) examples/counter.sol

# Bouncing discs on a canvas, and the one example that draws.
circles: all
	$(SOLVEIG)/bin/solis --extension=$(TARGET) examples/circles.sol

edit: all
	$(SOLVEIG)/bin/solis --extension=$(TARGET) examples/edit.sol

clean:
	rm -rf $(BUILD)
