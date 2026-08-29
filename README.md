# solveig-gtk

A GTK4 window for [Solum](https://github.com/hansolovkarlsson/Solveig), loaded
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

Needs GTK4 and a checkout of Solveig 0.36.0 or later.

```sh
brew install gtk4                 # macOS
apt install libgtk-4-dev          # Debian, Ubuntu

make                              # -> build/gtk.so
make SOLVEIG=/path/to/Solveig     # if it is not ../Solveig
make run                          # build and open the counter
```

`make` says which of the two is missing rather than letting the compiler produce
an error that does not name the cause.

**A bundle is per-platform and per-build.** `SOL_EXTENSION_ABI` is compared for
equality and refused, never guessed — `SolValue` is passed by value and
`SolObject`'s layout is exposed, so nearly any struct change in the VM moves the
number. If `solvm` is rebuilt from a newer Solveig, rebuild this too:

```
solvm: cannot load extension build/gtk.so: refused ABI 1 --
built against a different SolVM, rebuild it against this one
```

You never rebuild `solvm` to add an extension. You do rebuild extensions when
`solvm` changes.

## Reference

Fifteen messages. Every one that changes a widget answers that widget, so calls
chain.

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

### What happens next

| | |
| --- | --- |
| `gtk:onClick(button, block)` | Answers the button. The block takes no arguments. |
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

## How much of GTK4 this is

**About half a percent of it**, and that is deliberate. The numbers, because
"a subset" could mean anything:

| | |
| --- | --- |
| `gtk_*` functions exported by libgtk-4 | **4,299** |
| distinct ones this extension calls | **21** |
| messages it publishes | **15** |

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
