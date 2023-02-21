#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef i32 bool32;

typedef struct {
	u8 *memory;
	XImage *ximage;
    
	i32 width;
	i32 height;
	i32 size;
	i32 pitch;
	i32 bytes_per_pixel;
} WindowOffscreenBuffer;

void
set_size_hint(Display *display, Window window,
			  int min_width, int min_height,
			  int max_width, int max_height)
{
	XSizeHints hints = {0};
	if(min_width > 0 && min_height > 0) {
		hints.flags |= PMinSize;
	}
    
	if(max_width > 0 && max_height > 0) {
		hints.flags |= PMaxSize;
	}
    
	hints.min_width = min_width;
	hints.min_height = min_height;
	hints.max_width = max_width;
	hints.max_height = max_height;
    
	XSetWMNormalHints(display, window, &hints);
}

Status
toggle_maximize(Display *display, Window window)
{
	Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
	Atom max_h = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
	Atom max_v = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    
	// NOTE(jake): according to the spec if "_NET_WM_STATE" isn't found,
	//			   then everything associated with it won't be available either
	if(wm_state == None)
		return 0;
    
	XClientMessageEvent ev = {0};
	ev.type = ClientMessage;
	ev.format = 32;
	ev.window = window;
	ev.message_type = wm_state;
	ev.data.l[0] = 2; // _NET_WM_STATE_TOGGLE
	ev.data.l[1] = max_h;
	ev.data.l[2] = max_v;
	ev.data.l[3] = 1;
    
	return XSendEvent(display, DefaultRootWindow(display), False,
					  SubstructureNotifyMask,
					  (XEvent *) &ev);
}

void
resize_window_buffer(Display *display, XVisualInfo *visual_info, WindowOffscreenBuffer *buffer, i32 new_width, i32 new_height)
{
	buffer->width = new_width;
	buffer->height = new_height;
	buffer->size = buffer->width * buffer->height * buffer->bytes_per_pixel;
	buffer->pitch = buffer->width * buffer->bytes_per_pixel;
	
	if(buffer->ximage) {
		// NOTE(jake): this also frees window_offscreen_buffer.memory
		XDestroyImage(buffer->ximage);
	}
	
	buffer->memory = malloc(buffer->size);
	buffer->ximage = XCreateImage(display, visual_info->visual, visual_info->depth,
								  ZPixmap, 0, (char *)buffer->memory,
								  buffer->width, buffer->height,
								  32, 0);
}

void
render_gradient(WindowOffscreenBuffer *buffer, i32 xoffset, i32 yoffset)
{
	u8* row = buffer->memory;
	for(int y = 0; y < buffer->height; y++) {
		u32 *pixel = (u32 *)row;
		for(int x = 0; x < buffer->width; x++) {
			u8 blue = x + xoffset;
			u8 green = y + yoffset;
			u8 red = (y + x) + yoffset;
            
			*pixel++ = (red << 16) | (green << 8) | blue;
		}
		row += buffer->pitch;
	}
}

int
main(int argc, char **argv)
{
	Display *display = XOpenDisplay(0);
	if(!display) {
		printf("No display available\n");
		exit(1);
	}
    
	int root = DefaultRootWindow(display);
	int default_screen = DefaultScreen(display);
    
	XVisualInfo visual_info = {0};
	if(!XMatchVisualInfo(display, default_screen, 24, TrueColor, &visual_info)) {
		printf("No matching visual info\n");
		exit(1);
	}
    
	XSetWindowAttributes window_attributes = {0};
	// NOTE(jake): when set to ForgetGravity X discards the window state every resize, causing a flicker.
	//             also have to add CWBitGravity to the attribute mask.
	window_attributes.bit_gravity = StaticGravity;
	window_attributes.background_pixel = 0;
	window_attributes.colormap = XCreateColormap(display, root, visual_info.visual, AllocNone);
	window_attributes.event_mask = StructureNotifyMask | KeyPressMask | KeyReleaseMask;
	// NOTE(jake): this tells X what we defined in window_attributes when passed to XCreateWindow. super cool api!!
	u32 attribute_mask = CWBackPixel | CWColormap | CWEventMask | CWBitGravity;
    
	i32 window_width = 1280;
	i32 window_height = 720;
    
	Window window = XCreateWindow(display, root,
								  0, 0,
								  window_width, window_height, 0,
								  visual_info.depth, InputOutput,
								  visual_info.visual, attribute_mask, &window_attributes);
    
	// NOTE(jake): sets minimum window size to 400 x 300
	set_size_hint(display, window, 400, 300, 0, 0);
	XStoreName(display, window, "Hello, World!");
    
	XMapWindow(display, window);
	// toggle_maximize(display, window); // NOTE(jake): call to maximize the window on startup
	XFlush(display);
    
	WindowOffscreenBuffer window_offscreen_buffer = {0};
	window_offscreen_buffer.width = window_width;
	window_offscreen_buffer.height = window_height;
	window_offscreen_buffer.bytes_per_pixel = 4;
	window_offscreen_buffer.size = window_width * window_height * 4;
	window_offscreen_buffer.pitch = window_width * 4;
	window_offscreen_buffer.memory = (u8 *)malloc(window_offscreen_buffer.size);
	window_offscreen_buffer.ximage = XCreateImage(display, visual_info.visual, visual_info.depth,
												  ZPixmap, 0, (char *)window_offscreen_buffer.memory,
												  window_width, window_height,
												  32, 0);
	
	GC default_gc = DefaultGC(display, default_screen);
	
	Atom WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", False);
	if(!XSetWMProtocols(display, window, &WM_DELETE_WINDOW, 1)) {
		fprintf(stderr, "Couldn't register WM_DELETE_WINDOW property\n");
	}
    
    i32 xoffset = 0;
	i32 yoffset = 0;
	bool32 running = 1;
	while(running) {
		XEvent ev = {0};
		while(XPending(display)) {
			XNextEvent(display, &ev);
			switch(ev.type) {
			case DestroyNotify: {
				XDestroyWindowEvent *e = (XDestroyWindowEvent *) &ev;
					
				// NOTE(jake): this only is needed if opening multiple windows,
				// can be safely removed if you plan to only manage one window.
				if(e->window == window) {
					running = 0;
				}
			} break;
                
			case ClientMessage: {
				XClientMessageEvent *e = (XClientMessageEvent *) &ev;
				if(e->data.l[0] == WM_DELETE_WINDOW) {
					running = 0;
				}
			} break;
                
			case ConfigureNotify: {
				XConfigureEvent *e = (XConfigureEvent *) &ev;
				window_offscreen_buffer.width = e->width;
				window_offscreen_buffer.height = e->height;
				resize_window_buffer(display, &visual_info,
									 &window_offscreen_buffer,
									 e->width, e->height);
			} break;
                
			case KeyPress: {
				XKeyPressedEvent *e = (XKeyPressedEvent *) &ev;
                    
				// NOTE(jake): find keycode macros in: /usr/include/X11/keysymdef.h
				if(e->keycode == XKeysymToKeycode(display, XK_space)) {
					printf("You pressed the space\n");
				}
			} break;
				
			case KeyRelease: {
				XKeyPressedEvent *e = (XKeyPressedEvent *) &ev;
                    
				if(e->keycode == XKeysymToKeycode(display, XK_space)) {
					printf("You released the space\n");
				}
			} break;
			}
		}
        
		render_gradient(&window_offscreen_buffer, xoffset, yoffset);
		XPutImage(display, window,
				  default_gc, window_offscreen_buffer.ximage,
				  0, 0, 0, 0,
				  window_offscreen_buffer.width, window_offscreen_buffer.height);
		xoffset++;
		yoffset += 2;
	}
    
	XUnmapWindow(display, window);
	XDestroyWindow(display, window);
	XCloseDisplay(display);
	return 0;
}
