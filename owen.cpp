#include "owen.hpp"
#include <glog/logging.h>

using ::std::unique_ptr;

bool WindowManager::wm_detected_;

unique_ptr<WindowManager> WindowManager::Create() {
	// 1. Open X dinsplay.
	Display* display = XOpenDisplay(nullptr);
	
	if (display == nullptr) {
		LOG(ERROR) << "Failed to open X display " << XDisplayName(nullptr);
		return nullptr;
	}

	// 2. Construct WindowManager instance.
	return unique_ptr<WindowManager>(new WindowManager(display));
}

WindowManager::WindowManager(Display* display)
	: display_(CHECK_NOTNULL(display)),
	root_(DefaultRootWindow(display_)) {
}

WindowManager::~WindowManager() {
	XCloseDisplay(display_);
}

void WindowManager::Run() {
	// 1. Initialization
	wm_detected_ = false;
	XSetErrorHandler(&WindowManager::OnWMDetected);
	XSelectInput(
			display_,
			root_,
			SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask);

	XSync(display_, false);

	if (wm_detected_) {
		LOG(ERROR) << "Detected another window manager on display "
		        << XDisplayString(display_);
		return;
	}
	// b. Set error handler.
	XSetErrorHandler(&WindowManager::OnXError);

	// c. Grab X server to prevent windows from changing under us while we
	// frame them.
	XGrabServer(display_);

	// d. Frame existing top-level windows.
	// 	i. Query existing top-level windows.
	Window returned_root, returned_parent;
	Window* top_level_windows;
	unsigned int num_top_level_windows;
	CHECK(XQueryTree(
				display_,
				root_,
				&returned_root,
				&returned_parent,
				&top_level_windows,
				&num_top_level_windows));
	CHECK_EQ(returned_root, root_);
	//	ii. Frame each top-level window.
	for (unsigned int i = 0; i < num_top_level_windows; ++i) {
		Frame(top_level_windows[i], true);
	}
	//	iii. Free top-level window array.
	XFree(top_level_windows);
	
	// Ungrab X server.
	XUngrabServer(display_);

	// 2. Main event loop.
	for (;;) {
		// Get next event.
		XEvent e;
		XNextEvent(display_, &e);
		LOG(INFO) << "Received event: "/* << ToString(e)*/;

		// Dispatch event.
		switch (e.type) {
			case CreateNotify:
				OnCreateNotify(e.xcreatewindow);
				break;
			case ConfigureRequest:
				OnConfigureRequest(e.xconfigurerequest);
				break;
			case MapRequest:
				OnMapRequest(e.xmaprequest);
				break;
			case MapNotify:
				OnMapNotify(e.xmap);
				break;
			case DestroyNotify:
				OnDestroyNotify(e.xdestroywindow);
				break;
			case ReparentNotify:
				OnReparentNotify(e.xreparent);
				break;
			case ConfigureNotify:
				OnConfigureNotify(e.xconfigure);
				break;
			case ButtonPress:
				OnButtonPress(e.xbutton);
				break;
			case MotionNotify:
				while (XCheckTypedWindowEvent(display_, e.xmotion.window, MotionNotify, &e)) {}
				OnMotionNotify(e.xmotion);
				break;
			case UnmapNotify:
				OnUnmapNotify(e.xunmap);
				break;
			default:
				LOG(WARNING) << "Ignored event.";
		}
	}
}

void WindowManager::OnCreateNotify(const XCreateWindowEvent& e) {}

void WindowManager::OnDestroyNotify(const XDestroyWindowEvent& e) {}

void WindowManager::OnConfigureRequest(const XConfigureRequestEvent& e) {
	XWindowChanges changes;

	changes.x = e.x;
	changes.y = e.y;
	changes.width = e.width;
	changes.height = e.height;
	changes.border_width = e.border_width;
	changes.sibling = e.above;
	changes.stack_mode = e.detail;

	if (clients_.count(e.window)) {
		const Window frame = clients_[e.window];
		XConfigureWindow(display_, frame, e.value_mask, &changes);
		LOG(INFO) << "Resize [" << frame << "] to "/* << Size<int>(e.width, e.height)*/;
	}

	XConfigureWindow(display_, e.window, e.value_mask, &changes);
	LOG(INFO) << "Resize " << e.window << " to "/* << Size<int>(e.width, e.height)*/;
}

void WindowManager::OnMapRequest(const XMapRequestEvent& e) {
	Frame(e.window, false);
	XMapWindow(display_, e.window);
}

void WindowManager::Frame(Window w, bool was_created_before_window_manager) {
	LOG(INFO) << "NEW WINDOE" << w;
	const unsigned int BORDER_WIDTH = 4;
	const unsigned long BORDER_COLOR = 0xff5a5f;
	const unsigned long BG_COLOR = 0xbfd7ea;

	XWindowAttributes x_window_attrs;
	CHECK(XGetWindowAttributes(display_, w, &x_window_attrs));

	// 2. If window was created before window manager started, we should frame it only if
	// it is visible and doesn't set override_redirect.
	if (was_created_before_window_manager) {
		if (x_window_attrs.override_redirect || x_window_attrs.map_state != IsViewable) {
			return;
		}
	}
	
	// 3. Create frame
	const Window frame = XCreateSimpleWindow(
			display_,
			root_,
			50,
			50,
			x_window_attrs.width,
			x_window_attrs.height,
			BORDER_WIDTH,
			BORDER_COLOR,
			BG_COLOR);

	// 3. Select events on frame.
	XSelectInput(
			display_,
			frame,
			SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask);

	// XGrabButton(
	// 	display_,
	// 	Button1,
	// 	Mod1Mask ,
	// 	w,
	// 	true,
	// 	ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
	// 	GrabModeAsync,
	// 	GrabModeAsync,
	// 	None,
	// 	None
	// );

	XGrabButton(
		display_,
		Button1,
		AnyModifier,
		w,
		true,
		ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
		GrabModeAsync,
		GrabModeAsync,
		None,
		None
	);

	// 4. Add client to save set, in case of a crash.
	XAddToSaveSet(display_, w);

	// 5. Reparent client window.
	XReparentWindow(
			display_,
			w,
			frame,
			0, 0);

	// 6. Map frame.
	XMapWindow(display_, frame);

	// 7. Save frame handle.
	clients_[w] = frame;

	LOG(ERROR) << "Framed window " << w << " [" << frame << "]";
}

void WindowManager::OnMotionNotify(const XMotionEvent& e) {
	const Window frame = clients_[e.window];
	const int delta_x = e.x_root - drag_start_pos_x;
	const int delta_y = e.y_root - drag_start_pos_y;

	if (e.state & Button1Mask) {
		LOG(ERROR) << "DRAG";
		const int new_x = drag_start_frame_pos_x + delta_x;
		const int new_y = drag_start_frame_pos_y + delta_y;

		XMoveWindow(
			display_,
			frame,
			new_x, new_y
		);
	}
}

void WindowManager::OnReparentNotify(const XReparentEvent& e) {}

void WindowManager::OnMapNotify(const XMapEvent& e) {}

void WindowManager::OnConfigureNotify(const XConfigureEvent& e) {}

void WindowManager::OnUnmapNotify(const XUnmapEvent& e) {
	if (!clients_.count(e.window)) {
		LOG(INFO) << "Ignore UnmapNotify for non-client window " << e.window;
		return;
	}

	if (e.event == root_) {
		return;
	}

	Unframe(e.window);
}

void WindowManager::Unframe(Window w) {
	const Window frame = clients_[w];

	XUnmapWindow(display_, frame);

	XReparentWindow(
			display_,
			w,
			root_,
			0,0);

	XRemoveFromSaveSet(display_, w);

	XDestroyWindow(display_, frame);
	clients_.erase(w);

	LOG(INFO) << "Unframed window " << w << " [" << frame << "]";
}

void WindowManager::OnButtonPress(const XButtonEvent& e) {
	LOG(ERROR) << "Button Clicked";
	Window frame = clients_[e.window];

	// Save positions in case of a drag.
	drag_start_pos_x = e.x_root;
	drag_start_pos_y = e.y_root;

	// Save initial window attrs.
	Window returned_root;
	int x, y;
	unsigned width, height, border_width, depth;
	XGetGeometry(
		display_,
		frame,
		&returned_root,
		&x, &y,
		&width, &height,
		&border_width,
		&depth);

	drag_start_frame_pos_x = x;
	drag_start_frame_pos_y = y;

	drag_start_frame_height = height;
	drag_start_frame_width = width;

	XRaiseWindow(display_, frame);
}

int WindowManager::OnWMDetected(Display* display, XErrorEvent* e) {
	CHECK_EQ(static_cast<int>(e->error_code), BadAccess);

	wm_detected_ = true;

	return 0;
}

int WindowManager::OnXError(Display* display, XErrorEvent* e) {
	LOG(INFO) << "ERROR";
	return 0;
}
