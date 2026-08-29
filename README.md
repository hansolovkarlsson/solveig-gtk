# solveig-gtk

A GTK4 window for [Solveig](https://github.com/hansolovkarlsson/Solveig), loaded
at run time.

```sh
make
../Solveig/bin/solvm --extension=build/gtk.so examples/counter.sob
```

```
gtk:start.

window := gtk:window("Counter", #320, #200).
shown  := gtk:label("0").
up     := gtk:button("Count up").

gtk:onClick(up, {
    count := @expr(count + #1).
    gtk:setText(shown, count:asString) }).

box := gtk:box('vertical, #8).
gtk:add(box, shown).
gtk:add(box, up).

gtk:setChild(window, box).
gtk:show(window).
gtk:run.
```

## Why this is a separate repository

Solveig's front page says *no dependencies beyond a C11 compiler and `make`*,
and that sentence is checked on three platforms by CI rather than asserted. It
stays true because GTK lives here. Nothing in Solveig needs GTK installed to
build, to test, or to ship — and a machine that has never heard of GTK builds
the language exactly as before.

That is the whole point of the extension mechanism rather than a consequence of
it. A **host** is a binary, so *n* capabilities means 2<sup>n</sup> binaries; a
**bundle** is a file, so *n* capabilities means *n* files and the combination is
chosen when the program starts:

```sh
solvm --extension=gtk.so --extension=bignum.so --extension=net.so program.sob
```

Any subset, any order, no arrangement between them. They meet at the root object
and nowhere else, the way `system` and `array` do.

## Building

Needs GTK4 and **[Solveig 0.36.0](https://github.com/hansolovkarlsson/Solveig/releases/tag/v0.36.0)
or later** — the release the extension interface arrived in.

Either a clone or the release tarball will do. The build reads headers straight
out of the tree, so an unpacked `solveig-0.36.0/` works as `SOLVEIG` with
nothing else done to it:

```sh
curl -LO https://github.com/hansolovkarlsson/Solveig/releases/download/v0.36.0/solveig-0.36.0.tar.gz
tar xzf solveig-0.36.0.tar.gz && make -C solveig-0.36.0

brew install gtk4                 # macOS
apt install libgtk-4-dev          # Debian, Ubuntu

make SOLVEIG=solveig-0.36.0       # -> build/gtk.so;  default is ../Solveig
make run SOLVEIG=solveig-0.36.0   # build and counter
```

**The version is checked rather than taken on trust**, because the failure it
prevents is unhelpful: an older checkout has no `solum/extend.h` at all, so the
compiler says a header is missing and says nothing about why.

```
solveig-gtk: found Solveig 0.35.0 under ../Solveig,
  and the extension interface arrived in 0.36.0.
  Update that checkout, or point SOLVEIG at a newer one.
```

A missing GTK4 is named the same way rather than left to the compiler.

**That is a build-time check for one thing only** — *is there an extension
interface at all*. The run-time half is separate and stays separate: a bundle is
per-platform and per-build, and `SOL_EXTENSION_ABI` is compared for equality
when it loads, never guessed, since `SolValue` is passed by value and
`SolObject`'s layout is exposed. So rebuild this whenever `solvm` is rebuilt
from a newer Solveig:

```
solvm: cannot load extension build/gtk.so: refused ABI 1 --
built against a different SolVM, rebuild it against this one
```

You never rebuild `solvm` to add an extension. You do rebuild extensions when
`solvm` changes.

## Reference

Seventeen messages. Every one that changes a widget answers that widget, so
calls chain.

### Opening and closing

| | |
| --- | --- |
| `gtk:start` | Opens the toolkit. Answers `true`, or fails with *no display to open a window on*. Calling it twice is harmless. Everything else fails until it has been called. |
| `gtk:run` | Hands the program to GTK until the last window closes. Answers `nil`. Returns immediately if no window is open. |
| `gtk:quit` | Ends `gtk:run` early, from inside a handler. Answers `nil`. |

### Making widgets

| | |
| --- | --- |
| `gtk:window(title, #width, #height)` | A window, not yet shown. `title` is a string. |
| `gtk:label(text)` | |
| `gtk:button(text)` | |
| `gtk:box('vertical, #spacing)` | Or `'horizontal`. Carries a 12px margin. |

Each answers a **foreign handle** — `<gtk widget>` when printed.

### Arranging them

| | | |
| --- | --- | --- |
| `gtk:add(box, child)` | answers the box | first argument must be a box |
| `gtk:setChild(window, child)` | answers the window | a window holds one child; use a box for more |
| `gtk:show(window)` | answers the window | |
| `gtk:close(window)` | answers the window | exactly what the close button does |

### Text

| | |
| --- | --- |
| `gtk:setText(widget, text)` | On a label, a button or a window. Answers the widget. |
| `gtk:text(widget)` | Answers a string; `""` where GTK has none. |
| `gtk:setMarkup(label, markup)` | Pango markup rather than plain text — `<tt>` for monospace, `<span background=…>` for a highlight. The caller escapes its own text; invalid markup is refused rather than drawn wrong. |

### What happens next

| | |
| --- | --- |
| `gtk:onClick(button, block)` | Answers the button. The block takes no arguments. |
| `gtk:onKey(window, block)` | Answers the window. The block takes one argument: an object with `event:key` (GDK's name — `"Escape"`, `"Left"`, `"a"`) and `event:text` (what typing it produces, or `nil`). |
| `gtk:every(#milliseconds, block)` | Answers `nil`. Runs the block on a timer until it answers `false`. |

### Failures

Every message checks its own arity and its argument types, and says which
message and what it got:

```
'window' expects an integer, got float -- sizes are written with '#'
'add' expects a box as its first argument
'onClick' expects a block, got nil
'label' before gtk:start -- the toolkit has not been opened
```

A widget from another extension is refused the same way, because the foreign
handle carries a kind and it is checked.

## What the program does not say

**There is no `load` anywhere in the `.sol` file.** `gtk` is a global, bound
before the run by whoever started the program. Run without the bundle, the same
file fails at the first line that names it:

```
solvm: undefined name 'gtk'
  [examples/counter.sol:14] in script
```

That is deliberate. Native code runs past `--steps`, past `--memory`, past
everything — so whether to grant it is a decision belonging to whoever starts
the program, not to the script. See Solveig's `docs/extensions.md`.

## Limits still apply to a program with a window

Which is not obvious, and is the reason `gtk:run` checks the machine's error
flag after every callback rather than only after the ones that look risky:

```sh
$ solvm --steps=400 --extension=build/gtk.so counter.sob
solvm: stopped: the step limit of 400 was reached
  [counter.sol:6] in block
  [counter.sol:7] in script
$ echo $?
124
```

A limit-stop is uncatchable and sets the error flag. A main loop that did not
look would keep calling into a machine that had already been stopped — the one
way an extension can defeat `--steps`. So every handler here checks, quits the
loop, and hands the program back to `sol_vm_run` to report the way it would have
without a window involved.

The same goes for an ordinary failure inside a handler:

```
solvm: undefined name 'nosuchname'
  [err.sol:5] in block
  [err.sol:6] in script
```

— naming the block *and* the `gtk:run` line beneath it.

## How the callbacks stay alive

This is the part an extension gets wrong, and it was found by getting it wrong.

A block handed to GTK as `user_data` lives in a C struct, and the collector
walks the value stack, the frames, the temporary roots and the class objects —
none of which is that. So a collection between one click and the next sweeps the
block, and the next click runs **whatever now occupies that cell**. The measured
failure was:

```
probe: callback failed: 'block' takes 1 argument, got 0
```

an arity complaint about a block the program never registered. Not a crash, and
nothing in it pointing at the collector.

Every handler here keeps a `SolRetained` **token** instead, and looks the block
up when it fires. A released token answers *false* where a stale value would
answer a plausible wrong block. `g_signal_connect_data`'s destroy notify is what
releases it when the widget goes away, so a program that opens and closes a
thousand dialogs retains nothing.

## Two GTK4 facts this is written around

Both were found by clicking the close button, which is the one path the tests
here cannot take — and a bug in it shipped in the first commit because of that.

**`GtkWidget::destroy` does not exist in GTK4.** Connecting to it silently does
nothing. The first version counted open windows there, so the count never moved:
the window vanished and the command kept running.

**And `close-request`'s default handler hides the window rather than destroying
it.** GTK4 leaves window lifetime to `GtkApplication` — which owns `main`, which
an extension does not have. So closing is finished by hand: destroy the window,
count it, and answer `TRUE` to say it is dealt with.

`gtk:close` exists because of this. It does exactly what the close button does,
which means the path can now be taken by a program rather than only by a person:

```
gtk:every(#600, { gtk:close(w). false }).
gtk:run.
"clean exit":print.
```

## The editor, ported

[examples/edit.sol](examples/edit.sol) is Solveig's own
[modal terminal editor](https://github.com/hansolovkarlsson/Solveig/blob/main/programs/edit.sol)
— 1,766 lines, vi-like, with motions, operators, undo, marks, search and
`:s///` — running in a window. It was ported to find out what this bundle was
missing, which is what both this repository and Solveig's notes say the trigger
for more messages is.

**The model did not move.** Of 1,766 lines, the diff removes 33 and adds 114,
most of them comments explaining the port. The buffer, the motions, the undo
stack, the operators and the whole of `:s///` are the file as it was.

### What had to change: three things, and only one of them was interesting

**`screen:measure` became a constant.** The terminal version asked
`system:terminalSize` every frame — a message added to Solveig *for* this
program. A window is a grid the program chooses, so the ask is gone.

**`edit:render` writes markup instead of ANSI.** It composed one string and
called `system:write` once; it now composes the same string and calls
`gtk:setMarkup`. Cursor-home, erase-line and reverse-video become `<tt>` and two
spans — and the cursor, which a terminal drew for free by *moving* one, is now a
cell with a background.

**And the loop inverted, which is the whole of the port that is not mechanical.**
The terminal version owned its loop: draw, block on a key, act. GTK owns the
loop and calls in, so the body turns inside out — act on the key GTK brings,
then draw — and `gtk:run` is where the program waits. Nothing above the driver
knows it happened.

### What the port deleted

The terminal's hardest problem: **`escape` cannot be told from the first byte of
an arrow.** That is why Solveig has `system:keyWaiting`, why `edit:escapeWait` is
fifty milliseconds, and why `edit:decodeEscape` exists at all.

GTK delivers a decoded key. `Escape` is `"Escape"` and an arrow is `"Left"`. So
`decode`, `decodeEscape` and `escapeWait` are gone, and the port is *shorter*
where the terminal was hardest.

### What it asked Solveig for, and got

One thing: **`string:replace`**. Escaping `&`, `<` and `>` for markup wanted it
three times in one line, and the language had none. `split` then `join` was the
workaround — exact rather than approximate, since that pair is what a replace
*does* — so the port shipped with it and the absence was written down instead of
worked around silently.

It is in Solveig now, and this file uses it. That is the same trigger rule
running the other way: a program wanted something the *language* did not have,
so the language grew by one message rather than by a wishlist.

### What it asked this bundle for

`gtk:onKey` and `gtk:setMarkup`, which is why they exist. A key arrives as an
object with `event:key` (GDK's name for it) and `event:text` (what typing it
produces, or nil) — two fields because a binding needs the first and an insert
mode needs the second, and neither can be guessed from the other.

That is the trigger firing exactly as designed: **a program wanted something the
bundle did not have, and the bundle grew by two messages rather than by four
hundred.**

## How much of GTK4 this is

**About half a percent of it**, and that is deliberate. The numbers, because
"a subset" could mean anything:

| | |
| --- | --- |
| `gtk_*` functions exported by libgtk-4 | **4,299** |
| distinct ones this extension calls | **26** |
| messages it publishes | **17** |

**What is missing, in the order you will miss it.** No entry and no text view,
so there is no way to type anything. No list, tree or grid, and no layout beyond
a single box. No dialogs, no menus, no CSS, no drawing area, no
`GtkApplication`. You can build a window that shows things and reacts to
clicks. You cannot build a form.

**This is a demonstration that the mechanism carries a real toolkit, not a
binding to write an application against.** It exists because two things about a
real toolkit were genuinely uncertain — a foreign main loop calling back into
the VM, and widget lifetimes against a garbage collector — and neither could be
settled by a smaller example.

### Adding more is mechanical now, which is the point

The expensive work was per-*toolkit*, not per-function, and it is done:

- widget lifetimes against the collector — solved once, in `wrap()`
- the main loop re-entering the VM — solved once
- callbacks surviving collection — solved once, through the retain registry
- limits still applying — solved once, by checking `had_error`

A new message is a primitive, an arity check, a `sol_foreign_handle` call and a
line in `sol_extension_init`. Fifteen lines. The first fifteen messages took a
day; the next fifty would take an afternoon and no design at all.

### And at GTK's size, by hand is probably the wrong way

GTK ships **GObject Introspection** data — `Gtk-4.0.gir` describes **3,348
methods** in machine-readable XML, with types, ownership transfer and
nullability. That is what every other language's GTK binding is generated from
rather than typed out. A generator emitting `sol_object_define_primitive` calls
from the GIR is the obvious way to go past a few dozen messages.

**Nothing here is waiting on that.** The trigger is a program that wants
something this does not have — which is the same rule that decided every other
question in this project, and the reason there are fifteen messages rather than
four hundred.

## Licence

MIT, the same as Solveig.
