; hello.sol -- the smallest thing that opens a window.
gtk:start.
w := gtk:window("Hello", #240, #120).
gtk:setChild(w, gtk:label("Hello from Solum")).
gtk:show(w).
gtk:run.
