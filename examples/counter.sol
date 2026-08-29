; counter.sol -- a window, a label, two buttons and a clock.
;
; Nothing here says "load GTK". `gtk` is a global, put there before the run by
; whoever started the program:
;
;     solvm --extension=build/gtk.so examples/counter.sob
;
; Started without it, this fails at line 12 with `undefined name 'gtk'` rather
; than at load -- which is the whole of how an extension is reached.

count := #0.
ticks := #0.

gtk:start.

window := gtk:window("Counter", #320, #200).
box    := gtk:box('vertical, #8).

shown := gtk:label("0").
clock := gtk:label("waiting").

; -- The blocks below are held by GTK, not by this program. They survive
; -- collection because the extension retains them; see docs/extensions.md.
up := gtk:button("Count up").
gtk:onClick(up, {
    count := @expr(count + #1).
    gtk:setText(shown, count:asString) }).

reset := gtk:button("Reset").
gtk:onClick(reset, {
    count := #0.
    gtk:setText(shown, "0") }).

gtk:add(box, shown).
gtk:add(box, clock).
gtk:add(box, up).
gtk:add(box, reset).

gtk:setChild(window, box).
gtk:show(window).

; -- A timer, which answers false when it is done and stops itself.
gtk:every(#1000, {
    ticks := @expr(ticks + #1).
    gtk:setText(clock, "up ":concat(ticks:asString):concat("s")).
    true }).

gtk:run.

"counted to ":display. count:print.
