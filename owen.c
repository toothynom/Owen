#include <X11/Xlib.h>
#include <X11/cursorfont.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

// Class Definitions
typedef struct {
    int x, y;
} Point;

typedef struct {
    Window win;
    Window frame;
} Client;

typedef struct {
    int index, height, width, client_count;
    Client clients[256];
} OwenScreen;

typedef struct {
    Display* dpy;
    Window root;

    OwenScreen screen;

    bool wm_detected;
} Owen;

// Function prototypes
//      Error handlers
static int _on_vm_detected_error(Display* dpy, XErrorEvent* e);
static int _on_x_error(Display* dpy, XErrorEvent* e);

//      Owen functions
static void owen_init();
static void owen_reframe();
static void owen_frame(Window win, bool is_refreamed);
static void owen_unframe(Window win);
static void owen_run();
static void owen_destroy();

static void owen_clients_add(Window win, Window frame);
static int  owen_clients_get_index(Window win);
static void owen_clients_delete(Window win);

static int  owen_frame_get_width();
static int  owen_frame_get_height();

//      X Event Handlers
static void x_on_unmap_notify(const XUnmapEvent e);
static void x_on_map_request(const XMapRequestEvent e);
static void x_on_configure_request(const XConfigureRequestEvent e);

// Static definitions
static Owen owen;

int _on_vm_detected(Display* dpy, XErrorEvent* e) {
    if (e->error_code == BadAccess) {
        owen.wm_detected = true;
    }

    return 0;
}

int _on_x_error(Display* dpy, XErrorEvent* e) {

    return 0;
}

int owen_frame_get_width() {
    if (owen.screen.client_count == 0) {
        return owen.screen.width;
    } else {
        return owen.screen.width / 2;
    }
}

int owen_frame_get_height() {
    if (owen.screen.client_count == 0) {
        return owen.screen.height;
    } else {
        return owen.screen.height / owen.screen.client_count;
    }
}

Point owen_frame_get_start_point(const int index) {
    Point point;

    if (index == 0) {
        point.x = 0;
    } else {
        point.x = owen.screen.width / 2;
    }

    if (index == 0 || index == 1) {
        point.y = 0;
    } else {
        point.y = owen.screen.height / index;
    }
    return point;
}

void owen_clients_add(Window win, Window frame) {
    Client client;
    client.win = win;
    client.frame = frame;
    owen.screen.clients[owen.screen.client_count] = client;
    owen.screen.client_count++;
}

int owen_clients_get_index(Window win) {
    for (int i = 0; i < owen.screen.client_count; i++) {
        if (win == owen.screen.clients[i].win) {
            return i;
        }
    }

    return -1;
}

// Deletes a client window by remapping the array and decreasing total client count.
void owen_clients_delete(Window win) {
    for (int i = 0; i < owen.screen.client_count; i++) {
        if (win == owen.screen.clients[i].win) {
            owen.screen.clients[i] = owen.screen.clients[owen.screen.client_count - 1];
            owen.screen.client_count--;
            break;
        }
    }
}

void owen_frame(Window win, bool is_reframed) {
    const unsigned int BORDER_WIDTH = 4;
    const unsigned long BORDER_COLOR = 0xff5a5f;
    const unsigned long BG_COLOR = 0xbfd7ea;

    XWindowAttributes x_window_attrs;
    XGetWindowAttributes(owen.dpy, win, &x_window_attrs);

    // 2. If window was created before window manager started, we should frame it only if
    // it is visible and doesn't set override_redirect.
    if (is_reframed) {
        if (x_window_attrs.override_redirect || x_window_attrs.map_state != IsViewable) {
            return;
        }
    }

    const int new_width = owen_frame_get_width();
    const int new_height = owen_frame_get_height();
    const Point point = owen_frame_get_start_point(owen.screen.client_count);

    // 3. Create frame
    const Window frame = XCreateSimpleWindow(
        owen.dpy,
        owen.root,
        point.x, point.y,
        new_width,
        new_height,
        BORDER_WIDTH,
        BORDER_COLOR,
        BG_COLOR);

    // 3. Select events on frame.
    XSelectInput(
        owen.dpy,
        frame,
        SubstructureRedirectMask | SubstructureNotifyMask
    );

    // 4. Add client to save set, in case of a crash.
    XAddToSaveSet(owen.dpy, win);

    // 5. Reparent client window.
    XReparentWindow(
        owen.dpy,
        win,
        frame,
        0, 0);

    // 6. Map frame.
    XMapWindow(owen.dpy, frame);

    // 7. Save frame handle.
    owen_clients_add(win, frame);
}

void owen_unframe(Window win) {
    const Window frame = owen.screen.clients[owen_clients_get_index(win)].frame;
    XUnmapWindow(owen.dpy, frame);

    XReparentWindow(
        owen.dpy,
        win,
        owen.root,
        0,0);

    XRemoveFromSaveSet(owen.dpy, win);

    XDestroyWindow(owen.dpy, frame);
    owen_clients_delete(win);
}

void x_on_unmap_notify(const XUnmapEvent e) {
    if (owen_clients_get_index(e.window) == -1) {
        return;
    }

    if (e.event == owen.root) {
        return;
    }

    owen_unframe(e.window);
}

void x_on_map_request(const XMapRequestEvent e) {
    owen_frame(e.window, false);
    XMapWindow(owen.dpy, e.window);
}

void x_on_configure_request(const XConfigureRequestEvent e) {
    XWindowChanges changes;

    changes.x = e.x;
    changes.y = e.y;
    changes.width = e.width;
    changes.height = e.height;
    changes.border_width = e.border_width;
    changes.sibling = e.above;
    changes.stack_mode = e.detail;

    const int owen_client_index = owen_clients_get_index(e.window);

    if (owen_client_index != -1) {
        const Window frame = owen.screen.clients[owen_client_index].frame;
        XConfigureWindow(owen.dpy, frame, e.value_mask, &changes);
    }

    XConfigureWindow(owen.dpy, e.window, e.value_mask, &changes);
}

// Initialize connection to X server and subscribe for events
void owen_init() {
    owen.dpy = XOpenDisplay(NULL);

    if (owen.dpy == NULL) {
        printf("Owen: Failed to open X display\n");
        exit(-1);
    }

    owen.root = DefaultRootWindow(owen.dpy);
    owen.wm_detected = false;

    XSetErrorHandler(_on_vm_detected);
    XSelectInput(
        owen.dpy,
        owen.root,
        SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask);

    XSync(owen.dpy, false);

    if (owen.wm_detected) {
        printf("Owen: Another WM is running\n");
        exit(-1);
    }

    XSetErrorHandler(_on_x_error);

    // Change default cursor style XC_left_ptr 68
    Cursor cursor = XCreateFontCursor(owen.dpy, XC_left_ptr);
    XDefineCursor(owen.dpy, owen.root, cursor);

    owen.screen.client_count = 0;
    owen.screen.index = DefaultScreen(owen.dpy);
    owen.screen.height = DisplayHeight(owen.dpy, owen.screen.index);
    owen.screen.width = DisplayWidth(owen.dpy, owen.screen.index);
}

// Reframe already existing windows
void owen_reframe() {
    // c. Grab X server to prevent windows from changing under us while we
    // frame them.
    XGrabServer(owen.dpy);

    // d. Frame existing top-level windows.
    // 	i. Query existing top-level windows.
    Window returned_root, returned_parent;
    Window* top_level_windows;
    unsigned int num_top_level_windows;
    XQueryTree(
        owen.dpy,
        owen.root,
        &returned_root,
        &returned_parent,
        &top_level_windows,
        &num_top_level_windows);

    //	ii. Frame each top-level window.
    for (unsigned int i = 0; i < num_top_level_windows; ++i) {
        owen_frame(top_level_windows[i], true);
    }
    //	iii. Free top-level window array.
    XFree(top_level_windows);

    // Ungrab X server.
    XUngrabServer(owen.dpy);
}

void owen_run() {
    XEvent e;

    while(1) {
        XNextEvent(owen.dpy, &e);

        switch (e.type) {
            case MapRequest:
                x_on_map_request(e.xmaprequest);
                break;
            case ConfigureRequest:
                x_on_configure_request(e.xconfigurerequest);
                break;
            case UnmapNotify:
                x_on_unmap_notify(e.xunmap);
                break;
        }
    }
}

void owen_destroy() {
    XCloseDisplay(owen.dpy);
}

// Main
int main() {
    owen_init();
    owen_reframe();
    owen_run();
    owen_destroy();

    return 0;
}
