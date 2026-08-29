/* gtk.c -- a GTK4 window for Solveig, loaded at run time.
 *
 * Built as a shared object and named when a program is started:
 *
 *     solvm --extension=build/gtk.so program.sob
 *
 * The program never says "load GTK". `gtk` is a global, bound before the run,
 * sitting beside `system` and `array`. A program started without the bundle
 * fails at the first line that names it.
 *
 * See Solveig's docs/extensions.md for the contract this is written to, and
 * solum/extend.h for the four rules. Three of them show up on every page of
 * this file:
 *
 *   1. arity is not checked for you              -- `args()` below
 *   2. failure is out of band                    -- sol_vm_runtime_error
 *   4. check had_error after calling a block     -- every signal handler
 *
 * Rule 3 is the interesting one and it is why `sol_extension_retain` exists: a
 * block handed to GTK as `user_data` is reachable from nothing the collector
 * walks, so a collection between one click and the next would sweep it and the
 * next click would run whatever landed in that cell. Every handler here keeps a
 * *token* and looks the block up when it fires.
 *
 * **Widget ownership.** A GTK widget is a refcounted GObject, and the ones
 * created here arrive with a floating reference. `g_object_ref_sink` turns that
 * into a reference this extension owns, which is what the foreign cell then
 * releases. A parent taking a child adds a reference of its own, so the two
 * lifetimes do not fight: the program may drop a widget it built and the window
 * keeps it alive, and the reverse.
 */
#include <gtk/gtk.h>

#include "solum/extend.h"

/* The kind every widget is wrapped as. One word rather than one per class,
   because `sol_foreign_handle` answers what a primitive then type-checks with
   GTK's own macros -- which know about inheritance and a string does not. */
#define WIDGET "gtk widget"

/* Whether gtk_init has run. GTK may not be initialised twice, and a program
   that forgets is a better error than a crash inside GTK. */
static gboolean started;

/* How many windows are open. The loop below runs until this reaches zero, which
   is what "the program ends when you close its last window" means -- and what a
   GtkApplication would have done for us, at the cost of owning `main`, which an
   extension does not have. */
static int windows_open;
static GMainLoop *loop;

/* ---- the shapes every primitive starts with ------------------------------ */

/* Rule 1: arity is not checked for you. */
static bool args(SolVM *vm, const char *name, int argc, int wanted)
{
    if (argc == wanted) return true;
    sol_vm_runtime_error(vm, "'%s' takes %d argument%s, got %d",
                         name, wanted, wanted == 1 ? "" : "s", argc);
    return false;
}

static bool wants_string(SolVM *vm, const char *name, SolValue value)
{
    if (SOL_IS_STRING(value)) return true;
    sol_vm_runtime_error(vm, "'%s' expects a string, got %s",
                         name, sol_type_name(value));
    return false;
}

static bool wants_integer(SolVM *vm, const char *name, SolValue value)
{
    if (SOL_IS_INT(value)) return true;
    sol_vm_runtime_error(vm, "'%s' expects an integer, got %s -- "
                         "sizes are written with '#'", name, sol_type_name(value));
    return false;
}

static bool wants_block(SolVM *vm, const char *name, SolValue value)
{
    if (SOL_IS_BLOCK(value)) return true;
    sol_vm_runtime_error(vm, "'%s' expects a block, got %s",
                         name, sol_type_name(value));
    return false;
}

/* A widget out of a foreign handle, or NULL with the failure already reported.
   The `kind` check is what stops another extension's socket arriving here. */
static GtkWidget *widget_of(SolVM *vm, const char *name, SolValue value)
{
    void *handle = sol_foreign_handle(value, WIDGET);
    if (handle == NULL) {
        sol_vm_runtime_error(vm, "'%s' expects a widget, got %s",
                             name, sol_type_name(value));
        return NULL;
    }
    return GTK_WIDGET(handle);
}

static bool ready(SolVM *vm, const char *name)
{
    if (started) return true;
    sol_vm_runtime_error(vm, "'%s' before gtk:start -- "
                         "the toolkit has not been opened", name);
    return false;
}

/* ---- wrapping a widget --------------------------------------------------- */

static void drop_widget(void *handle)
{
    /* The reference taken by ref_sink below. GTK may still hold others -- a
       parent's, or its own for a toplevel -- and this says only that the
       program has stopped pointing at it. */
    g_object_unref(handle);
}

/* A widget is a few hundred bytes of GObject plus whatever it draws with, and
   none of that is knowable from here. Zero says so rather than guessing: a
   wrong footprint makes `--memory` lie, and the collector's own pressure count
   for foreign cells is what keeps them from piling up regardless. */
#define WIDGET_FOOTPRINT 0

static SolValue wrap(SolVM *vm, GtkWidget *widget)
{
    g_object_ref_sink(widget);
    return SOL_FOREIGN_VAL(sol_foreign_new(vm, widget, drop_widget,
                                           WIDGET, WIDGET_FOOTPRINT));
}

/* ---- callbacks ----------------------------------------------------------- */

/* What a connected block needs at the moment it fires. The token rather than
   the block: a released token answers false, where a stale SolValue would
   answer a plausible wrong block. */
typedef struct {
    SolVM      *vm;
    SolRetained block;
} Handler;

static Handler *handler_new(SolVM *vm, SolValue block)
{
    Handler *handler = g_new(Handler, 1);
    handler->vm = vm;
    handler->block = sol_extension_retain(vm, block);
    return handler;
}

/* GTK calls this when the widget the handler was attached to goes away, which
   is where the block stops being kept alive. Without it a program that opened
   and closed a thousand dialogs would retain a thousand blocks. */
static void handler_free(gpointer data, GClosure *closure)
{
    (void)closure;
    Handler *handler = data;
    sol_extension_release(handler->vm, handler->block);
    g_free(handler);
}

/* Runs the block a handler stands for, and answers whether the machine is still
   healthy afterwards.
 *
 * Rule 4, and it is not a formality here: a limit-stop sets `had_error` and is
 * deliberately not catchable, so a main loop that did not look would keep
 * calling into a machine that has already been stopped. The loop is quit
 * instead, which gives the program back to `sol_vm_run` and lets it report what
 * happened the way it would have without a window involved. */
static bool fire(Handler *handler, SolValue *args_in, int argc)
{
    SolValue block;
    if (!sol_extension_retained(handler->vm, handler->block, &block)) {
        return true;             /* released; nothing to call, nothing wrong */
    }

    sol_vm_call_block(handler->vm, block, args_in, argc);
    if (handler->vm->had_error) {
        if (loop != NULL) g_main_loop_quit(loop);
        return false;
    }
    return true;
}

static void on_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    fire((Handler *)data, NULL, 0);
}

static gboolean on_timeout(gpointer data)
{
    Handler *handler = data;
    SolValue block;
    if (!sol_extension_retained(handler->vm, handler->block, &block)) {
        return G_SOURCE_REMOVE;
    }

    SolValue answer = sol_vm_call_block(handler->vm, block, NULL, 0);
    if (handler->vm->had_error) {
        if (loop != NULL) g_main_loop_quit(loop);
        return G_SOURCE_REMOVE;
    }
    /* `false` stops the timer, anything else keeps it -- the shape `whileTrue`
       has, so it reads the way a Solum loop reads. */
    if (SOL_IS_BOOL(answer) && !SOL_AS_BOOL(answer)) {
        sol_extension_release(handler->vm, handler->block);
        g_free(handler);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* The close button, and the two GTK4 facts this has to be written around.
 *
 * **`GtkWidget::destroy` is gone in GTK4.** Connecting to it silently does
 * nothing, which is what the first version of this file did -- so the counter
 * below never moved and `gtk:run` never returned. The window vanished and the
 * command did not: found by clicking, which is the one path the tests here
 * cannot take.
 *
 * **And `close-request`'s default handler hides the window rather than
 * destroying it.** GTK4 leaves window lifetime to GtkApplication, which owns
 * `main` and which an extension therefore cannot use. So closing has to be
 * finished by hand: destroy it, count it, and answer TRUE to say it is dealt
 * with. */
static gboolean on_close_request(GtkWindow *window, gpointer data)
{
    (void)data;
    gtk_window_destroy(window);
    if (--windows_open <= 0 && loop != NULL) g_main_loop_quit(loop);
    return TRUE;
}

/* ---- opening and closing the toolkit ------------------------------------- */

/* gtk:start -- answers true, or fails saying there is no display. */
static SolValue prim_start(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self; (void)a;
    if (!args(vm, "start", argc, 0)) return SOL_NIL_VAL;

    if (started) return SOL_BOOL_VAL(true);      /* asking twice is harmless */

    if (!gtk_init_check()) {
        sol_vm_runtime_error(vm, "gtk:start -- no display to open a window on");
        return SOL_NIL_VAL;
    }
    started = TRUE;
    return SOL_BOOL_VAL(true);
}

/* gtk:run -- hands the program to GTK until the last window closes.
 *
 * One instruction as far as the machine is concerned, which is worth knowing:
 * `--steps` counts instructions, and the ones spent here are the ones inside
 * the blocks GTK calls back into. A program that opens a window and waits is
 * not spending its allowance while it waits. */
static SolValue prim_run(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self; (void)a;
    if (!args(vm, "run", argc, 0)) return SOL_NIL_VAL;
    if (!ready(vm, "run")) return SOL_NIL_VAL;

    if (windows_open <= 0) return SOL_NIL_VAL;   /* nothing to wait for */

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    loop = NULL;
    return SOL_NIL_VAL;
}

/* gtk:quit -- leave the loop early, from inside a handler. */
static SolValue prim_quit(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self; (void)a;
    if (!args(vm, "quit", argc, 0)) return SOL_NIL_VAL;
    if (loop != NULL) g_main_loop_quit(loop);
    return SOL_NIL_VAL;
}

/* ---- widgets ------------------------------------------------------------- */

/* gtk:window("title", #width, #height) */
static SolValue prim_window(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "window", argc, 3)) return SOL_NIL_VAL;
    if (!ready(vm, "window")) return SOL_NIL_VAL;
    if (!wants_string(vm, "window", a[0])) return SOL_NIL_VAL;
    if (!wants_integer(vm, "window", a[1])) return SOL_NIL_VAL;
    if (!wants_integer(vm, "window", a[2])) return SOL_NIL_VAL;

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), SOL_AS_STRING(a[0])->chars);
    gtk_window_set_default_size(GTK_WINDOW(window),
                                (int)SOL_AS_INT(a[1]), (int)SOL_AS_INT(a[2]));

    windows_open++;
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), NULL);
    return wrap(vm, window);
}

static SolValue prim_label(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "label", argc, 1)) return SOL_NIL_VAL;
    if (!ready(vm, "label")) return SOL_NIL_VAL;
    if (!wants_string(vm, "label", a[0])) return SOL_NIL_VAL;
    return wrap(vm, gtk_label_new(SOL_AS_STRING(a[0])->chars));
}

static SolValue prim_button(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "button", argc, 1)) return SOL_NIL_VAL;
    if (!ready(vm, "button")) return SOL_NIL_VAL;
    if (!wants_string(vm, "button", a[0])) return SOL_NIL_VAL;
    return wrap(vm, gtk_button_new_with_label(SOL_AS_STRING(a[0])->chars));
}

/* gtk:box('vertical, #spacing) -- or 'horizontal. A symbol rather than an
   integer, because `#0` and `#1` at a call site say nothing. */
static SolValue prim_box(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "box", argc, 2)) return SOL_NIL_VAL;
    if (!ready(vm, "box")) return SOL_NIL_VAL;
    if (!SOL_IS_SYMBOL(a[0])) {
        sol_vm_runtime_error(vm, "'box' expects 'vertical or 'horizontal");
        return SOL_NIL_VAL;
    }
    if (!wants_integer(vm, "box", a[1])) return SOL_NIL_VAL;

    const char *how = SOL_AS_SYMBOL(a[0])->chars;
    GtkOrientation orientation;
    if (strcmp(how, "vertical") == 0)        orientation = GTK_ORIENTATION_VERTICAL;
    else if (strcmp(how, "horizontal") == 0) orientation = GTK_ORIENTATION_HORIZONTAL;
    else {
        sol_vm_runtime_error(vm, "'box' expects 'vertical or 'horizontal, got '%s",
                             how);
        return SOL_NIL_VAL;
    }

    GtkWidget *box = gtk_box_new(orientation, (int)SOL_AS_INT(a[1]));
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    return wrap(vm, box);
}

/* ---- putting them together ----------------------------------------------- */

static SolValue prim_add(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "add", argc, 2)) return SOL_NIL_VAL;

    GtkWidget *box = widget_of(vm, "add", a[0]);
    GtkWidget *child = widget_of(vm, "add", a[1]);
    if (box == NULL || child == NULL) return SOL_NIL_VAL;

    if (!GTK_IS_BOX(box)) {
        sol_vm_runtime_error(vm, "'add' expects a box as its first argument");
        return SOL_NIL_VAL;
    }
    gtk_box_append(GTK_BOX(box), child);
    return a[0];                          /* the box, so adds can be chained */
}

static SolValue prim_set_child(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "setChild", argc, 2)) return SOL_NIL_VAL;

    GtkWidget *window = widget_of(vm, "setChild", a[0]);
    GtkWidget *child = widget_of(vm, "setChild", a[1]);
    if (window == NULL || child == NULL) return SOL_NIL_VAL;

    if (!GTK_IS_WINDOW(window)) {
        sol_vm_runtime_error(vm, "'setChild' expects a window");
        return SOL_NIL_VAL;
    }
    gtk_window_set_child(GTK_WINDOW(window), child);
    return a[0];
}

static SolValue prim_show(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "show", argc, 1)) return SOL_NIL_VAL;

    GtkWidget *window = widget_of(vm, "show", a[0]);
    if (window == NULL) return SOL_NIL_VAL;
    if (!GTK_IS_WINDOW(window)) {
        sol_vm_runtime_error(vm, "'show' expects a window");
        return SOL_NIL_VAL;
    }
    gtk_window_present(GTK_WINDOW(window));
    return a[0];
}

/* gtk:close(window) -- exactly what the close button does, and here so that the
   path can be taken by a test rather than only by a person. Its absence is why
   a bug in that path shipped. */
static SolValue prim_close(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "close", argc, 1)) return SOL_NIL_VAL;

    GtkWidget *window = widget_of(vm, "close", a[0]);
    if (window == NULL) return SOL_NIL_VAL;
    if (!GTK_IS_WINDOW(window)) {
        sol_vm_runtime_error(vm, "'close' expects a window");
        return SOL_NIL_VAL;
    }
    gtk_window_close(GTK_WINDOW(window));
    return a[0];
}

/* ---- reading and writing text -------------------------------------------- */

static SolValue prim_set_text(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "setText", argc, 2)) return SOL_NIL_VAL;
    if (!wants_string(vm, "setText", a[1])) return SOL_NIL_VAL;

    GtkWidget *widget = widget_of(vm, "setText", a[0]);
    if (widget == NULL) return SOL_NIL_VAL;

    const char *text = SOL_AS_STRING(a[1])->chars;
    if (GTK_IS_LABEL(widget))       gtk_label_set_text(GTK_LABEL(widget), text);
    else if (GTK_IS_BUTTON(widget)) gtk_button_set_label(GTK_BUTTON(widget), text);
    else if (GTK_IS_WINDOW(widget)) gtk_window_set_title(GTK_WINDOW(widget), text);
    else {
        sol_vm_runtime_error(vm, "'setText' expects a label, a button or a window");
        return SOL_NIL_VAL;
    }
    return a[0];
}

static SolValue prim_text(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "text", argc, 1)) return SOL_NIL_VAL;

    GtkWidget *widget = widget_of(vm, "text", a[0]);
    if (widget == NULL) return SOL_NIL_VAL;

    const char *text = NULL;
    if (GTK_IS_LABEL(widget))       text = gtk_label_get_text(GTK_LABEL(widget));
    else if (GTK_IS_BUTTON(widget)) text = gtk_button_get_label(GTK_BUTTON(widget));
    else if (GTK_IS_WINDOW(widget)) text = gtk_window_get_title(GTK_WINDOW(widget));
    else {
        sol_vm_runtime_error(vm, "'text' expects a label, a button or a window");
        return SOL_NIL_VAL;
    }
    if (text == NULL) text = "";
    return SOL_STRING_VAL(sol_string_new(vm, text, (int)strlen(text)));
}

/* ---- what happens next --------------------------------------------------- */

/* gtk:onClick(button, { ... }) */
static SolValue prim_on_click(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "onClick", argc, 2)) return SOL_NIL_VAL;
    if (!wants_block(vm, "onClick", a[1])) return SOL_NIL_VAL;

    GtkWidget *button = widget_of(vm, "onClick", a[0]);
    if (button == NULL) return SOL_NIL_VAL;
    if (!GTK_IS_BUTTON(button)) {
        sol_vm_runtime_error(vm, "'onClick' expects a button");
        return SOL_NIL_VAL;
    }

    /* `connect_data` rather than `connect`, for the destroy notify: it is what
       releases the retained block when the button goes away. */
    g_signal_connect_data(button, "clicked", G_CALLBACK(on_clicked),
                          handler_new(vm, a[1]), handler_free, 0);
    return a[0];
}

/* gtk:every(#milliseconds, { ... }) -- until the block answers false. */
static SolValue prim_every(SolVM *vm, SolValue self, SolValue *a, int argc)
{
    (void)self;
    if (!args(vm, "every", argc, 2)) return SOL_NIL_VAL;
    if (!wants_integer(vm, "every", a[0])) return SOL_NIL_VAL;
    if (!wants_block(vm, "every", a[1])) return SOL_NIL_VAL;
    if (SOL_AS_INT(a[0]) <= 0) {
        sol_vm_runtime_error(vm, "'every' wants a delay of 1 millisecond or more");
        return SOL_NIL_VAL;
    }

    g_timeout_add((guint)SOL_AS_INT(a[0]), on_timeout, handler_new(vm, a[1]));
    return SOL_NIL_VAL;
}

/* ---- installation -------------------------------------------------------- */

int sol_extension_init(SolVM *vm, int abi)
{
    if (abi != SOL_EXTENSION_ABI) return -1;

    SolObject *gtk = sol_object_new(vm, vm->object_class);

    sol_object_define_primitive(vm, gtk, "start",    prim_start);
    sol_object_define_primitive(vm, gtk, "run",      prim_run);
    sol_object_define_primitive(vm, gtk, "quit",     prim_quit);

    sol_object_define_primitive(vm, gtk, "window",   prim_window);
    sol_object_define_primitive(vm, gtk, "label",    prim_label);
    sol_object_define_primitive(vm, gtk, "button",   prim_button);
    sol_object_define_primitive(vm, gtk, "box",      prim_box);

    sol_object_define_primitive(vm, gtk, "add",      prim_add);
    sol_object_define_primitive(vm, gtk, "setChild", prim_set_child);
    sol_object_define_primitive(vm, gtk, "show",     prim_show);
    sol_object_define_primitive(vm, gtk, "close",    prim_close);

    sol_object_define_primitive(vm, gtk, "setText",  prim_set_text);
    sol_object_define_primitive(vm, gtk, "text",     prim_text);

    sol_object_define_primitive(vm, gtk, "onClick",  prim_on_click);
    sol_object_define_primitive(vm, gtk, "every",    prim_every);

    sol_vm_set_global(vm, "gtk", SOL_OBJ_VAL(gtk));
    return 0;
}
