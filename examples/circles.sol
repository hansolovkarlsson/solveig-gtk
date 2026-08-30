; circles.sol -- bouncing discs, the way a toolkit that owns the loop does it.
;
;     solvm --extension=build/gtk.so examples/circles.sob
;
; **This is the same program as solveig-sdl's examples/circles.sol, and it is
; not shaped like it at all.** That is the point of having both.
;
; SDL hands the program a frame and gets out of the way, so there the loop is an
; ordinary `whileTrue` that moves the balls, draws them, and presents. GTK owns
; the loop and calls into the program, so here there is no loop at all: `every`
; asks for the balls to be moved on a timer, `onDraw` answers with a picture
; whenever GTK wants one, and `gtk:run` is where the program waits.
;
; **Two consequences worth seeing.**
;
; Moving is separate from drawing. `every` changes where the balls are and says
; the picture is stale; `onDraw` reads that and draws. Neither knows when the
; other runs, and the frame is GTK's business rather than the program's -- so
; nothing here presents, clears, or thinks about a buffer.
;
; And a circle is one message. solveig-sdl has no `circle`: its surface draws
; rectangles and lines, so the disc there is worked out a row at a time. Cairo
; has `cairo_arc`, so this binding publishes it. Each says what its toolkit has.

gtk:start.

width  := #480.
height := #360.

; -- the balls, as parallel arrays. Solveig indexes from one.
xs := array:new.  ys := array:new.
dxs := array:new. dys := array:new.
rs := array:new.
reds := array:new. greens := array:new. blues := array:new.

palette := [[#240, #180, #60], [#90, #200, #160], [#220, #90, #120],
            [#120, #150, #240], [#200, #200, #90]].

count := #0.
i := #0.
speedX := #0. speedY := #0.
nx := #0. ny := #0.
colour := nil.

addBall := {
    count := count:inc.
    xs:add(nx). ys:add(ny).
    speedX := @expr(#2 + count:mod(#4)).
    speedY := @expr(#2 + count:mod(#3)).
    count:mod(#2):equals(#0):ifTrue({ speedX := speedX:negated }).
    count:mod(#3):equals(#0):ifTrue({ speedY := speedY:negated }).
    dxs:add(speedX). dys:add(speedY).
    rs:add(@expr(#12 + count:mod(#5) * #5)).
    colour := palette:at(@expr(count:mod(#5) + #1)).
    reds:add(colour:at(#1)). greens:add(colour:at(#2)). blues:add(colour:at(#3)) }.

nx := #120. ny := #90.  addBall:value.
nx := #300. ny := #140. addBall:value.
nx := #200. ny := #260. addBall:value.
nx := #380. ny := #220. addBall:value.

win  := gtk:window("circles", width, height).
area := gtk:canvas(width, height).

; ---------------------------------------------------------------------------
; What the picture is. GTK asks; this answers.
;
; **The block is given the canvas's real size, and that is the size to believe.**
; `gtk:canvas` asks for one; the window, the theme and the user resizing it all
; get a say. So the walls the balls bounce off are recorded here, from what
; arrived, rather than being the numbers that were requested -- which is why
; dragging the window bigger gives the balls more room instead of leaving them
; bouncing off an edge that is no longer there.

gtk:onDraw(area, { w, h |
    width := w. height := h.
    gtk:colour(#18, #19, #28).
    gtk:circle(@expr(w / #2), @expr(h / #2), @expr(w + h)).
    i := #1.
    { i:lessOrEqual(count) }:whileTrue({
        gtk:colour(reds:at(i), greens:at(i), blues:at(i)).
        gtk:circle(xs:at(i), ys:at(i), rs:at(i)).
        i := i:inc }) }).

; ---------------------------------------------------------------------------
; What changes, and how the picture is told.

gtk:every(#16, {
    i := #1.
    { i:lessOrEqual(count) }:whileTrue({
        nx := @expr(xs:at(i) + dxs:at(i)).
        ny := @expr(ys:at(i) + dys:at(i)).
        @expr(nx < rs:at(i)):or({ @expr(nx > width - rs:at(i)) }):ifTrue({
            dxs:atPut(i, dxs:at(i):negated).
            nx := @expr(xs:at(i) + dxs:at(i)) }).
        @expr(ny < rs:at(i)):or({ @expr(ny > height - rs:at(i)) }):ifTrue({
            dys:atPut(i, dys:at(i):negated).
            ny := @expr(ys:at(i) + dys:at(i)) }).
        xs:atPut(i, nx). ys:atPut(i, ny).
        i := i:inc }).
    gtk:redraw(area).
    true }).

; -- a key adds a ball, because there is no pointer message here to add one at
;    a position. `space` is the one to press.
gtk:onKey(win, { event |
    event:key:equals("space"):ifTrue({
        count:lessThan(#24):ifTrue({
            nx := @expr(#40 + count:mod(#7) * #55).
            ny := @expr(#40 + count:mod(#5) * #58).
            addBall:value }) }) }).

gtk:setChild(win, area).
gtk:show(win).
gtk:run.

"balls at the end: ":display. count:print.
