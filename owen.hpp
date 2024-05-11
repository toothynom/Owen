extern "C" {
#include <X11/Xlib.h>
}
#include <memory>
#include <unordered_map>

class WindowManager {
	public:
		// Factory method for extablishing a connection to an X server and creating a
		// WindowManager instance.
		static ::std::unique_ptr<WindowManager> Create();
		
		// Disconnects from the X server.
		~WindowManager();

		// The entry point to this class. Enters the main event loop.
		void Run();
	
	private:
		// Invoked internally by Create().
		WindowManager(Display* display);
		
		// Frames a top level window.
		void Frame(Window w, bool was_created_before_window_manager);

		// Unframes a client window.
		void Unframe(Window w);

		int drag_start_pos_x;
		int drag_start_pos_y;
		int drag_start_frame_pos_x;
		int drag_start_frame_pos_y;
		int drag_start_frame_height;
		int drag_start_frame_width;

		// EventHandlers.
		void OnCreateNotify(const XCreateWindowEvent& e);
		void OnDestroyNotify(const XDestroyWindowEvent& e);
		void OnReparentNotify(const XReparentEvent& e);
		void OnMapNotify(const XMapEvent& e);
		void OnConfigureNotify(const XConfigureEvent& e);
		void OnMapRequest(const XMapRequestEvent& e);
		void OnConfigureRequest(const XConfigureRequestEvent& e);
		void OnUnmapNotify(const XUnmapEvent& e);
		void OnButtonPress(const XButtonEvent& e);
		void OnMotionNotify(const XMotionEvent& e);

		// Handle to the underlying Xlib Display struct.
		Display* display_;

		// Handle to the root window.
		const Window root_;

		::std::unordered_map<Window, Window> clients_;
		
		static bool wm_detected_;
		// Xlib error handler.
		static int OnXError(Display* display, XErrorEvent* e);
		
		static int OnWMDetected(Display* display, XErrorEvent* e);
};
