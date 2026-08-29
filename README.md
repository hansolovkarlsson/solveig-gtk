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

## The messages

| | |
| --- | --- |
| `gtk:start` | open the toolkit; fails saying so when there is no display |
| `gtk:run` | hand the program to GTK until the last window closes |
| `gtk:quit` | leave the loop early, from inside a handler |
| `gtk:window(title, #width, #height)` | a window |
| `gtk:label(text)` `gtk:button(text)` | |
| `gtk:box('vertical, #spacing)` | or `'horizontal` |
| `gtk:add(box, child)` | answers the box, so adds chain |
| `gtk:setChild(window, child)` | |
| `gtk:show(window)` | |
| `gtk:setText(w, text)` `gtk:text(w)` | on a label, a button or a window |
| `gtk:onClick(button, block)` | |
| `gtk:every(#milliseconds, block)` | until the block answers `false` |

A widget is a **foreign handle** — `<gtk widget>` when printed, compared by
identity, answering `isKindOf(foreign)`. There is no `close` and no `destroy`:
the collector releases a widget the program has let go of, and the machine
releases what is still held when it goes down.

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

## What is not here

**Only one window's worth of widgets.** No entry, no list, no drawing area, no
menus, no CSS, no `GtkApplication`. This is the first bundle rather than a
binding, and it exists to prove the mechanism carries a real toolkit — which it
does, including the two things that were genuinely uncertain: a foreign main
loop calling back into the VM, and widget lifetimes against a garbage collector.

**No `GtkApplication`.** It wants to own `main`, and an extension does not have
one. `gtk:run` is a `GMainLoop` that ends when the last window closes.

**One thread.** A VM is one thread's, and GTK wants the main one. Neither is
negotiable and together they cost nothing today.

## Licence

MIT, the same as Solveig.
