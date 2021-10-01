/* See LICENSE file for copyright and license details.
 *
 * This is a minimal fork to dwm, aiming to be smaller, simpler
 * and friendlier.
 *
 * Filet-Lignux's dynamic window manager is designed like any other X client.
 * It is driven through handling X events. In contrast to other X clients,
 * a window manager selects for SubstructureRedirectMask on the root window,
 * to receive events about window (dis-)appearance. Only one X connection at a
 * time is allowed to select for this event mask.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list. Each client contains a bit array to indicate the tags (workspaces)
 * of a client.
 *
 * Keyboard shortcuts are organized as arrays.
 *
 * Mouse motion tracking governs window focus, along with
 * a click-to-raise behavior. Mouse motion is stateful and supports different
 * drag-modes for moving and resizing windows.
 *
 * Consult the included man page for further documentation. 
 *
 * To understand everything else, start reading main().
 */

#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/XF86keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

/* basic macros */
#define DIE(M) {fputs(M, stderr); exit(1);}
#define LOADCONF(P,C) ((*(void **)(&C)) = dlsym(dlopen(P, RTLD_LAZY), "config"))
                     /* leave the loaded lib in memory until process cleanup */
#define KEYMASK(mask) (mask & (ShiftMask|ControlMask|Mod1Mask|Mod4Mask))
#define KCODE(keysym) ((KeyCode)(XKeysymToKeycode(dpy, keysym)))
#define ISVISIBLE(C) ((C->tags & tagset))
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MOUSEINF(W,X,Y,M) (XQueryPointer(dpy,root,&dwin,&W,&X,&Y,&di,&di,&M))
#define PROPADD(P, W, A, T, S, V, E) {XChangeProperty(dpy, W, xatom[A], T,\
	S, PropMode##P,  (unsigned char *) V, E);}
#define PROPSET(W, A, T, S, V, E) PROPADD(Replace, W, A, T, S, V, E)
#define TEXTPAD (xfont->ascent + xfont->descent) /* side padding of text */
#define TEXTW(X) (drawgettextwidth(X) + TEXTPAD)

/* edge dragging and region macros*/
#define INZONE(C, X, Y) (X >= C->x - C->bw && Y >= C->y - C->bw\
	&& X <= C->x + WIDTH(C) + C->bw && Y <= C->y + HEIGHT(C) + C->bw)
#define MOVEZONE(C, X, Y) (INZONE(C, X, Y)\
	&& (abs(C->x - X) <= C->bw || abs(C->y - Y) <= C->bw))
#define RESIZEZONE(C, X, Y) (INZONE(C, X, Y)\
	&& (abs(C->x + WIDTH(C) - X) <= C->bw || abs(C->y + HEIGHT(C) - Y) <= C->bw))

/* monitor macros */
#define BARH (TEXTPAD + 2)
#define INMON(X, Y, M)\
	(X >= M.mx && X < M.mx + M.mw && Y >= M.my && Y < M.my + M.mh)
#define MONNULL(M) (M.mx == 0 && M.my == 0 && M.mw == 0 && M.mh == 0)
#define SETMON(M, R) {M.mx = R.x; M.my = R.y; M.mw = R.width; M.mh = R.height;}
#define ONMON(C, M) INMON(C->x + WIDTH(C)/2, C->y + HEIGHT(C)/2, M)

/* window macros */
#define HEIGHT(X) ((X)->h + 2 * (X)->bw)
#define WIDTH(X) ((X)->w + 2 * (X)->bw)

/* virtual desktop macros */
#define TAGMASK ((1 << tagslen) - 1)
#define TAGSHIFT(TAGS, I) (I < 0 ? (TAGS >> -I) | (TAGS << (tagslen + I))\
	: (TAGS << I) | (TAGS >> (tagslen - I)))

/* enums */
enum { fg, bg, mark, bdr, selbdr, colslen }; /* colors */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck, /* EWMH atoms */
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWinDialog, NetClientList, NetCliStack, NetLast,
       /* default atoms */
       WMProtocols, WMDelete, WMState, WMTakeFocus, XAtomLast };
/* bar click regions */
enum { ClkStatus, ClkTagBar, ClkLast };
/* mouse motion modes */
enum { DragMove, DragSize, DragTile, WinEdge, ZoomStack, CtrlNone };
/* window stack actions */
enum { CliPin, CliRaise, CliZoom, CliRemove, CliRefresh };

/* argument template for keyboard shortcut and bar click actions */
typedef union {
	int i;
	unsigned int ui;
	const void *v;
} Arg;

/* bar click action */
typedef struct {
	unsigned int click;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

/* clients wrap managed windows */
typedef struct Client Client;
struct Client {
	float mina, maxa;
	int x, y, w, h;
	int fx, fy, fw, fh; /*remember during tiled and fullscreen states */
	int basew, baseh, maxw, maxh, minw, minh;
	int bw, fbw, oldbw;
	unsigned int tags;
	int isfloating, fstate, isfullscreen;
	Client *next;
	Window win;
};

/* keyboard shortcut action */
typedef struct {
	unsigned int mod;
	KeySym key;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

/* A monitor could be a connected display.
 * You can also have multiple monitors across displays if you
 * want custom windowing regions. */
typedef struct { int mx, my, mw, mh; } Monitor; /* windowing region size */

/* function declarations callable from config plugins */
void focusstack(const Arg *arg);
void grabresize(const Arg *arg);
void grabstack(const Arg *arg);
void killclient(const Arg *arg);
void pin(const Arg *arg);
void quit(const Arg *arg);
void spawn(const Arg *arg);
void tag(const Arg *arg);
void togglefloating(const Arg *arg);
void togglefullscreen(const Arg *arg);
void toggletag(const Arg *arg);
void view(const Arg *arg);
void viewshift(const Arg *arg);
void viewtagshift(const Arg *arg);
void zoom(const Arg *arg);

/* event handler function declarations */
static void buttonpress(XEvent *e);
static void clientmessage(XEvent *e);
static void configurerequest(XEvent *e);
static void destroynotify(XEvent *e);
static void expose(XEvent *e);
static void exthandler(XEvent *ev);
static void focus(Client *c);
static void keypress(XEvent *e);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void propertynotify(XEvent *e);
static void unmapnotify(XEvent *e);

/* variables */
static char stxt[256] = { /* status text */
	'F','i','l','e','t','L','i','g','n','u','x','\0',[255]='\0'};
static int sw, sh;           /* X display screen geometry width, height */
static Window barwin;        /* the bar */
static int barfocus;         /* when the bar is forced raised */
static int ctrlmode = CtrlNone; /* mouse mode (resize/repos/arrange/etc) */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int tagset = 1; /* mask for which workspaces are displayed */
/* The event handlers are organized in an array which is accessed
 * whenever a new event has been fetched. The array indicies associate
 * with event numbers. This allows event dispatching in O(1) time.
 */
static void (*handler[LASTEvent]) (XEvent *) = { /* XEvent callbacks */
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[DestroyNotify] = destroynotify,
	[Expose] = expose,
	[GenericEvent] = exthandler,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify,
};
static int randroutputchange;     /* holds event-type: monitor change */
static Atom xatom[XAtomLast];     /* holds X types */
static int end;                   /* end session trigger (quit) */
static Display *dpy;              /* X session display reference */
static Drawable drawable;         /* canvas for drawing (bar) */
static XftDraw *drawablexft;      /* font rendering for canvas */
static GC gc;                     /* graphics context */
static Client *clients, *sel;     /* references to managed windows */
static Window root, wmcheckwin;
static Cursor curpoint, cursize;  /* mouse cursor icons */
static XftColor cols[colslen];    /* colors (fg, bg, mark, bdr, selbdr) */
static XftFont *xfont;            /* X font reference */
/* dummy variables */
static int di;
static unsigned long dl;
static unsigned int dui;
static Window dwin;

/***********************
* Configuration Section
* Allows config plugins to change config variables.
* The defaultconfig method has plugin compatible code but check the README
* for details including the flavors of the macros that will work in the plugin.
************************/
/* config assignment for single value variables */
#define S(T, N, V) N = V /* compatibily for plugin-flavor macro */
/* config assignment for arrays of variable length */
#define V(T, N, L, ...) do {static T _##N[] = __VA_ARGS__; N = _##N; L} while(0)
/* config assignment for pointer arrays with determinable length  */
#define P(T, N, ...) V(T,N,,__VA_ARGS__;)
/* config assignment for arrays (populating a xxxlen variable) */
#define A(T, N, ...) V(T,N,N##len = sizeof _##N/sizeof *_##N;,__VA_ARGS__;)

/* configurable values (see defaultconfig) */
Monitor *mons;
char *font, **colors, **tags, **launcher, **terminal, **upvol,
	**downvol, **mutevol, **suspend, **dimup, **dimdown, **help;
int borderpx, snap, tagslen, monslen, *nmain, keyslen, buttonslen;
int *barpos;
float *mfact;
KeySym stackrelease, barshow;
Key *keys;
Button *buttons;

void
defaultconfig(void)
{
	/* appearance */
	S(int, borderpx, 1); /* border pixel width of windows */
	S(int, snap, 8); /* edge snap pixel distance */
	S(char*, font, "monospace:size=8");
	/* colors (must be five colors: fg, bg, highlight, border, sel-border) */
	P(char*, colors, { "#dddddd", "#111111", "#335577", "#555555", "#dd4422" });

	/* virtual workspaces (must be 32 or less, *usually*) */
	A(char*, tags, { "1", "2", "3", "4", "5", "6", "7", "8", "9" });

	/* monitor layout
	   Set mons to the number of monitors you want supported.
	   Initialise with {0} for autodetection of monitors,
	   otherwise set the position and size ({x,y,w,h}).
	   !!!Warning!!! maximum of 32 monitors supported.
	   e.g:
	A(Monitor, mons, {
		{2420, 0, 1020, 1080},
		{1920, 0, 500, 1080},
		{3440, 0, 400,  1080}
	});
	   or to autodetect up to 3 monitors:
	A(Monitor, mons, {{0}, {0}, {0}});
	*/
	A(Monitor, mons, {{0}});
	/* position and width of the bar (x, y, w) */
	V(int, barpos,, {0, 0, 640});
	/* factor of main area size [0.05..0.95] (for each monitor) */
	P(float, mfact, {0.6});
	/* number of clients in main area (for each monitor) */
	P(int, nmain, {1});

	/* commands */
	P(char*, launcher, { "dmenu_run", "-p", ">", "-m", "0", "-i", "-fn",
		"monospace:size=8", "-nf", "#dddddd", "-sf", "#dddddd", "-nb", "#111111",
		"-sb", "#335577", NULL });
	P(char*, help, { "st", "-e", "bash", "-c",
		"man filetwm || man -l ~/.config/filetwmconf.1", NULL });
	P(char*, terminal, { "st", NULL });
	#define VOLCMD(A) ("amixer -q set Master "#A"; xsetroot -name \"Volume: "\
		"$(amixer sget Master | grep -m1 '%]' | "\
		"sed -e 's/[^\\[]*\\[\\([0-9]*%\\)[^\\[]*\\[\\([onf]*\\).*/\\1 \\2/')\"")
	P(char*, upvol, { "bash", "-c", VOLCMD("5%+"), NULL });
	P(char*, downvol, { "bash", "-c", VOLCMD("5%-"), NULL });
	P(char*, mutevol, { "bash", "-c", VOLCMD("toggle"), NULL });
	P(char*, suspend, {
		"bash", "-c", "killall slock; slock systemctl suspend -i", NULL });
	#define DIMCMD(A) ("xbacklight "#A" 5; xsetroot -name \"Brightness: "\
		"$(xbacklight | cut -d. -f1)%\"")
	P(char*, dimup, { "bash", "-c", DIMCMD("-inc"), NULL });
	P(char*, dimdown, { "bash", "-c", DIMCMD("-dec"), NULL });

	/* keyboard shortcut definitions */
	#define AltMask Mod1Mask
	#define WinMask Mod4Mask
	#define TK(KEY) { WinMask, XK_##KEY, view, {.ui = 1 << (KEY - 1)} }, \
	{       WinMask|ShiftMask, XK_##KEY, tag, {.ui = 1 << (KEY - 1)} }, \
	{                 AltMask, XK_##KEY, toggletag, {.ui = 1 << (KEY - 1)} },
	/* Alt+Tab style behaviour key release */
	S(KeySym, stackrelease, XK_Alt_L);
	/* Key to raise the bar for visibility when held down */
	S(KeySym, barshow, XK_Super_L);
	A(Key, keys, {
		/*               modifier / key, function / argument */
		{               WinMask, XK_Tab, spawn, {.v = &launcher } },
		{     WinMask|ShiftMask, XK_Tab, spawn, {.v = &terminal } },
		{             WinMask, XK_space, grabresize, {.i = DragMove } },
		{     WinMask|AltMask, XK_space, grabresize, {.i = DragSize } },
		{ WinMask|ControlMask, XK_space, togglefloating, {0} },
		{            AltMask, XK_Return, togglefullscreen, {0} },
		{            WinMask, XK_Return, pin, {0} },
		{    WinMask|AltMask, XK_Return, zoom, {0} },
		{               AltMask, XK_Tab, grabstack, {.i = +1 } },
		{     AltMask|ShiftMask, XK_Tab, grabstack, {.i = -1 } },
		{                WinMask, XK_Up, focusstack, {.i = -1 } },
		{              WinMask, XK_Down, focusstack, {.i = +1 } },
		{              WinMask, XK_Left, viewshift, {.i = -1 } },
		{             WinMask, XK_Right, viewshift, {.i = +1 } },
		{    WinMask|ShiftMask, XK_Left, viewtagshift, {.i = -1 } },
		{   WinMask|ShiftMask, XK_Right, viewtagshift, {.i = +1 } },
		{                 AltMask, XK_0, tag, {.ui = ~0 } },
		{                AltMask, XK_F4, killclient, {0} },
		{                WinMask, XK_F4, spawn, {.v = &suspend } },
		{ AltMask|ControlMask|ShiftMask, XK_F4, quit, {0} },
		{    0, XF86XK_AudioLowerVolume, spawn, {.v = &downvol } },
		{           0, XF86XK_AudioMute, spawn, {.v = &mutevol } },
		{    0, XF86XK_AudioRaiseVolume, spawn, {.v = &upvol } },
		{               0, XF86XK_Sleep, spawn, {.v = &suspend } },
		{     0, XF86XK_MonBrightnessUp, spawn, {.v = &dimup } },
		{   0, XF86XK_MonBrightnessDown, spawn, {.v = &dimdown } },
		TK(1) TK(2) TK(3) TK(4) TK(5) TK(6) TK(7) TK(8) TK(9)
	});

	/* bar actions */
	A(Button, buttons, {
		/* click,      button, function / argument */
		{ ClkStatus,   Button1, spawn, {.v = &help } },
		{ ClkTagBar,   Button1, view, {0} },
		{ ClkTagBar,   Button3, tag, {0} },
	});
}

/* End Configuration Section
****************************/

/***********************
* Utility functions
************************/

/**
 * Add a client to the top of the list.
 */
void
attach(Client *c)
{
	c->next = clients;
	clients = c;
}

/**
 * Update the X Server with a client's windowing details.
 */
void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

/**
 * Remove a specific client from the list.
 */
void
detach(Client *c)
{
	Client **tc;

	for (tc = &clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

/**
 * Query the X server for a window property.
 */
Atom
getatomprop(Client *c, Atom prop)
{
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

/**
 * Resize the window of the given client, if the values change,
 * respecting edge snapping, client specified sizing constraints,
 * and sizing implications of floating and fullscreen states.
 * The resulting size is stored on the client's size attributes, and
 * when in floating mode, the size before respecting
 * adjustments and constraints is stored on the client's f* size
 * attributes, for future reference.
 * Also, the border width of the window is updated depending
 * on the fullscreen state.
 */
void
resize(Client *c, int x, int y, int w, int h)
{
	int m1, m2;
	XWindowChanges wc;

	/* set minimum possible size */
	w = MAX(1, w);
	h = MAX(1, h);
	/* remain in visible area */
	x = MAX(1 - w - 2*c->bw, MIN(sw - 1, x));
	y = MAX(1 - h - 2*c->bw, MIN(sh - 1, y));

	if (c->isfloating && !c->isfullscreen) {
		c->fx = x;
		c->fy = y;
		c->fw = w;
		c->fh = h;
		/* snap position to edges */
		for (m1 = 0; m1 < monslen-1 && !INMON(x+snap, y+snap, mons[m1]); m1++);
		for (m2 = 0; m2 < monslen-1 && !INMON(x+w-snap, y+h-snap, mons[m2]); m2++);
		/* snap position */
		x = (abs(mons[m1].mx - x) < snap) ? mons[m1].mx : x;
		y = (abs(mons[m1].my - y) < snap) ? mons[m1].my : y;
		/* snap size */
		if (abs((mons[m2].mx + mons[m2].mw) - (x + w + 2*c->bw)) < snap)
			w = mons[m2].mx + mons[m2].mw - x - 2*c->bw;
		if (abs((mons[m2].my + mons[m2].mh) - (y + h + 2*c->bw)) < snap)
			h = mons[m2].my + mons[m2].mh - y - 2*c->bw;
	}

	/* adjust for aspect limits */
	/* see last two sentences in ICCCM 4.1.2.3 */
	w -= c->basew;
	h -= c->baseh;
	if (c->mina > 0 && c->maxa > 0 && !c->isfullscreen) {
		if (c->maxa < (float)w / h)
			w = h * c->maxa + 0.5;
		else if (c->mina < (float)h / w)
			h = w * c->mina + 0.5;
	}

	/* restore base dimensions and apply max and min dimensions */
	w = MAX(w + c->basew, c->minw);
	h = MAX(h + c->baseh, c->minh);
	w = (c->maxw && !c->isfullscreen) ? MIN(w, c->maxw) : w;
	h = (c->maxh && !c->isfullscreen) ? MIN(h, c->maxh) : h;

	/* apply the resize if anything ended up changing */
	if (x != c->x || y != c->y || w != c->w || h != c->h) {
		c->x = wc.x = x;
		c->y = wc.y = y;
		c->w = wc.width = w;
		c->h = wc.height = h;
		/* fullscreen changes update the border width */
		wc.border_width = c->bw;
		XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
		configure(c);
		XSync(dpy, False);
	}
}

/**
 * Reorders client window stack, front to back (respecting layers).
 * Stack layer order is pinned, selected, floating, tiled, then fullscreen.
 * Passing CliRefresh as the mode simply redraws
 * the windows in their existing order. All other
 * mode values changes the placement in the order
 * stack of the given client:
 *  - CliPin: pinned window to the very top above all layers (or unpin).
 *  - CliRaise: temporarily show above all layers, but below pinned.
 *  - CliZoom: bring the window to the top of stack.
 *  - CliRemove: Remove window from stack entirely.
 */
void
restack(Client *c, int mode)
{
	int i = 0;
	static Client *pinned = NULL, *raised = NULL;
	Window up[3];
	XWindowChanges wc;

	switch (mode) {
	case CliPin:
		/* toggle pinned state */
		pinned = pinned != c ? c : NULL;
		break;
	case CliRemove:
		detach(c);
		pinned = pinned != c ? pinned : NULL;
		raised = raised != c ? raised : NULL;
		break;
	case CliZoom:
		if (c) {
			detach(c);
			attach(c);
		}
		/* fall through to CliRaise */
	case CliRaise:
		raised = c;
	}
	/* always lift up anything pinned */
	if (pinned && pinned->isfloating) {
		detach(pinned);
		attach(pinned);
	}

	/* show window that sit above the standard layers */
	XDeleteProperty(dpy, root, xatom[NetCliStack]);
	if (barfocus) up[i++] = barwin;
	if (pinned) {
		up[i++] = pinned->win;
		PROPADD(Prepend, root, NetCliStack, XA_WINDOW, 32, &pinned->win, 1);
	}
	if (raised) {
		up[i++] = raised->win;
		PROPADD(Prepend, root, NetCliStack, XA_WINDOW, 32, &raised->win, 1);
	}
	if (!barfocus) up[i++] = barwin;
	XRaiseWindow(dpy, up[0]);
	XRestackWindows(dpy, up, i);
	wc.stack_mode = Below;
	wc.sibling = up[i - 1];

	/* order layers - floating then tiled then fullscreen (if not raised) */
	for (i = 0; i < 3; i++) /* i is layers: 0=floating, 1=tiled, 3=fullscreen */
		for (c = clients; c; c = c->next)
			if (c != pinned && c != raised && !c->isfloating + c->isfullscreen == i) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
				PROPADD(Prepend, root, NetCliStack, XA_WINDOW, 32, &c->win, 1);
			}
}

/**
 * Send a message to a cliant via the XServer.
 */
int
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols;
	int exists = 0;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = xatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

/**
 * Signal handler that ensures zombie subprocesses
 * are cleaned up immediately.
 */
void
sigchld(int unused)
{
	/* self-register this method as the SIGCHLD handler (if haven't already) */
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		DIE("can't install SIGCHLD handler.\n");

	/* immediately release resources associated with any zombie child */
	while (0 < waitpid(-1, NULL, WNOHANG));
}

/**
 * Retrieve size hint information for a client.
 * Stores the sizing information for the client
 * for future layout operations.
 */
void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	c->basew = c->baseh = c->maxw = c->maxh = c->minw = c->minh = 0;
	c->maxa = c->mina = 0.0;
	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		return;

	if (size.flags & PBaseSize) {
		c->basew = c->minw = size.base_width;
		c->baseh = c->minh = size.base_height;
	}
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	}
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	}
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	}
}

/**
 * Returns a pointer to the client which manages the given X window,
 * or NULL if the given X window is not a managed client.
 */
Client *
wintoclient(Window w)
{
	Client *c;

	for (c = clients; c && c->win != w; c = c->next);
	return c;
}

/**
 * Xlib error handler.
 * There's no way to check accesses to destroyed
 * windows, thus those cases are ignored (especially
 * on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit.
 */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	else if (ee->request_code == X_ChangeWindowAttributes
	&& ee->error_code == BadAccess)
		DIE("filetwm: another window manager may already be running.\n");
	fprintf(stderr, "filetwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

/**
 * Xlib error handler for ignoring all errors.
 */
int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/***********************
* General functions
************************/

void
arrange(void)
{
	Client *c;
	int m, h, mw;
	/* maximum of 32 monitors supported */
	int nm[32] = {0}, i[32] = {0}, my[32] = {0}, ty[32] = {0};

	/* ensure a visible window has focus */
	focus(NULL);

	/* hide and show clients for the current workspace */
	for (c = clients; c; c = c->next)
		XMoveWindow(dpy, c->win, ISVISIBLE(c) ? c->x : WIDTH(c) * -2, c->y);

	/* find the number of tiled clients in each monitor */
	for (c = clients; c; c = c->next)
		if (!c->isfloating && ISVISIBLE(c)) {
			for (m = monslen-1; m > 0 && !ONMON(c, mons[m]); m--);
			nm[m]++;
		}

	/* arrange tiled windows into the relevant monitors. */
	for (c = clients; c; c = c->next)
		if (!c->isfloating && ISVISIBLE(c)) {
			/* find the monitor placement again */
			for (m = monslen-1; m > 0 && !ONMON(c, mons[m]); m--);
			/* tile the client within the relevant monitor */
			mw = nm[m] > nmain[m] ? mons[m].mw * mfact[m] : mons[m].mw;
			if (i[m] < nmain[m]) {
				h = (mons[m].mh - my[m]) / (MIN(nm[m], nmain[m]) - i[m]);
				resize(c, mons[m].mx, mons[m].my + my[m], mw-(2*c->bw), h-(2*c->bw));
				if (my[m] + HEIGHT(c) < mons[m].mh)
					my[m] += HEIGHT(c);
			} else {
				h = (mons[m].mh - ty[m]) / (nm[m] - i[m]);
				resize(c, mons[m].mx + mw, mons[m].my + ty[m],
					mons[m].mw - mw - (2*c->bw), h - (2*c->bw));
				if (ty[m] + HEIGHT(c) < mons[m].mh)
					ty[m] += HEIGHT(c);
			}
			i[m]++;
		}

	/* Lift the selected window to the top */
	restack(sel, CliRaise);
}

int
drawgettextwidth(const char *text)
{
	XGlyphInfo ext;
	XftTextExtentsUtf8(dpy, xfont, (XftChar8*)text, strlen(text), &ext);
	return ext.xOff;
}

void
drawtext(int x, int y, int w, int h, const char *text, const XftColor *fg,
	const XftColor *bg)
{
	int ty = y + (h - (xfont->ascent + xfont->descent)) / 2 + xfont->ascent;

	XSetForeground(dpy, gc, bg->pixel);
	XFillRectangle(dpy, drawable, gc, x, y, w, h);
	XftDrawStringUtf8(drawablexft, fg, xfont, x + (TEXTPAD / 2), ty,
		(XftChar8 *)text, strlen(text));
}

void
drawbar()
{
	int i, x = 0;

	/* draw tags */
	for (i = 0; i < tagslen; i++) {
		drawtext(x, 0, TEXTW(tags[i]), BARH, tags[i], &cols[fg],
			&cols[tagset & 1 << i ? mark : bg]);
		x += TEXTW(tags[i]);
	}

	/* draw status */
	drawtext(x, 0, mons->mw, BARH, stxt, &cols[fg], &cols[bg]);

	/* display composited bar */
	XCopyArea(dpy, drawable, barwin, gc, 0, 0, barpos[2], BARH, 0, 0);
}

void
focus(Client *c)
{
	if ((!c || !ISVISIBLE(c)) && (!(c = sel) || !ISVISIBLE(sel)))
		for (c = clients; c && !ISVISIBLE(c); c = c->next);
	if (sel && sel != c) {
		/* catch the Click-to-Raise that could be coming */
		XGrabButton(dpy, AnyButton, AnyModifier, sel->win, False,
			ButtonPressMask, GrabModeSync, GrabModeSync, None, None);
		/* unfocus */
		XSetWindowBorder(dpy, sel->win, cols[bdr].pixel);
	}
	if (c) {
		XSetWindowBorder(dpy, c->win, cols[selbdr].pixel);
		if (!barfocus) {
			XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
			PROPSET(root, NetActiveWindow, XA_WINDOW, 32, &c->win, 1);
			sendevent(c, xatom[WMTakeFocus]);
		}
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, xatom[NetActiveWindow]);
	}
	sel = c;
	drawbar();
}

void
grabkeys(void)
{
	/* NumLock assumed to be Mod2Mask */
	unsigned int mods[] = { 0, LockMask, Mod2Mask, Mod2Mask|LockMask };

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (int i = 0; i < keyslen; i++)
		for (int j = 0; j < sizeof mods/sizeof *mods && KCODE(keys[i].key); j++)
			XGrabKey(dpy, KCODE(keys[i].key), keys[i].mod | mods[j], root,
				True, GrabModeAsync, GrabModeAsync);
}

void
grabresizeabort()
{
	int m;

	if (ctrlmode == CtrlNone || ctrlmode == ZoomStack)
		return;

	/* update the monitor layout to match any tiling changes */
	if (sel && ctrlmode == DragTile) {
		for (m = 0; m < monslen-1 && !INMON(sel->x, sel->y, mons[m]); m++);
		mfact[m] = MIN(0.95, MAX(0.05, (float)WIDTH(sel) / mons[m].mw));
		nmain[m] = MAX(1, mons[m].mh / HEIGHT(sel));
		arrange();
	}

	/* release the drag */
	XUngrabPointer(dpy, CurrentTime);
	ctrlmode = CtrlNone;
}

void
motion()
{
	int rx, ry, x, y;
	static int lx = 0, ly = 0;
	unsigned int mask;
	Window cw;
	static Window lastcw = {0};
	static Client *c = NULL;

	/* capture pointer and motion details */
	if (!MOUSEINF(cw, rx, ry, mask))
		return;
	x = rx - lx; lx = rx;
	y = ry - ly; ly = ry;

	/* handle any drag modes */
	if (sel && ctrlmode == DragMove)
		resize(sel, sel->fx + x, sel->fy + y, sel->fw, sel->fh);
	if (sel && ctrlmode == DragSize)
		resize(sel, sel->fx, sel->fy, MAX(sel->fw + x, 1), MAX(sel->fh + y, 1));
	if (sel && ctrlmode == DragTile)
		resize(sel, sel->x, sel->y, MAX(sel->w + x, 1), MAX(sel->h + y, 1));
	if (ctrlmode == WinEdge &&
		(!sel || (!MOVEZONE(sel, rx, ry) && !RESIZEZONE(sel, rx, ry))))
		grabresizeabort();
	if (ctrlmode != CtrlNone)
		return;

	c = cw != lastcw ? wintoclient(cw) : c;
	lastcw = cw;
	/* focus follows mouse */
	if (c && c != sel)
		focus(c);
	/* watch for border edge locations for resizing */
	if (c && !mask && (MOVEZONE(c, rx, ry) || RESIZEZONE(c, rx, ry)))
		grabresize(&(Arg){.i = WinEdge});
}

void
setfullscreen(Client *c, int fullscreen)
{
	int w, h, m1, m2;

	if (fullscreen && !c->isfullscreen) {
		PROPSET(c->win, NetWMState, XA_ATOM, 32, &xatom[NetWMFullscreen], 1);
		c->isfullscreen = 1;
		c->fstate = c->isfloating;
		c->fbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		/* find the full screen spread across the monitors */
		for (m1 = monslen-1; m1 > 0 && !INMON(c->x, c->y, mons[m1]); m1--);
		for (m2 = 0; m2 < monslen
		&& !INMON(c->x + WIDTH(c), c->y + HEIGHT(c), mons[m2]); m2++);
		if (m2 == monslen || mons[m2].mx + mons[m2].mw <= mons[m1].mx
		|| mons[m2].my + mons[m2].mh <= mons[m1].my)
			m2 = m1;
		/* apply fullscreen window parameters */
		w = mons[m2].mx - mons[m1].mx + mons[m2].mw;
		h = mons[m2].my - mons[m1].my + mons[m2].mh;
		resize(c, mons[m1].mx, mons[m1].my, w, h);
		restack(c, CliZoom);

	} else if (!fullscreen && c->isfullscreen){
		/* change back to original floating parameters */
		PROPSET(c->win, NetWMState, XA_ATOM, 32, 0, 0);
		c->isfullscreen = 0;
		c->isfloating = c->fstate;
		c->bw = c->fbw;
		resize(c, c->fx, c->fy, c->fw, c->fh);
	}
	arrange();
}

void
unmanage(Client *c, int destroyed)
{
	XWindowChanges wc;

	restack(c, CliRemove);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		XDeleteProperty(dpy, c->win, xatom[WMState]);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	sel = sel != c ? sel : NULL;
	free(c);
	XDeleteProperty(dpy, root, xatom[NetClientList]);
	for (c = clients; c; c = c->next)
		PROPADD(Append, root, NetClientList, XA_WINDOW, 32, &c->win, 1);
	arrange();
}

void
updatemonitors()
{
	int i, pri = 0, n;
	Monitor m;
	XRRMonitorInfo *inf;

	inf = XRRGetMonitors(dpy, root, 1, &n);
	for (i = 0; i < n && i < monslen; i++) {
		SETMON(mons[i], inf[i])
		if (inf[i].primary)
			pri = i;
	}
	/* push the primary monitor to the top */
	m = mons[pri];
	mons[pri] = mons[0];
	mons[0] = m;
}

void
updatestatus(void)
{
	char **v = NULL;
	XTextProperty p;

	if (XGetTextProperty(dpy, root, &p, XA_WM_NAME) && p.nitems) {
		if (XmbTextPropertyToTextList(dpy, &p, &v, &di) >= Success && *v) {
			strncpy(stxt, *v, sizeof(stxt) - 1);
			XFreeStringList(v);
		}
		XFree(p.value);
	}
	drawbar();
}

/***********************
* Event handler funcs
************************/

void
buttonpress(XEvent *e)
{
	int i, x = 0, click = ClkStatus;
	Client *c;
	Arg arg = {0};
	XButtonPressedEvent *ev = &e->xbutton;

	if (ev->window == barwin) {
		for (i = 0; i < tagslen && ev->x > (x += TEXTW(tags[i])); i++);
		if (i < tagslen) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		}
		for (i = 0; i < buttonslen; i++)
			if (click == buttons[i].click && buttons[i].button == ev->button)
				buttons[i].func(arg.ui ? &arg : &buttons[i].arg);
	}

	else if (sel && ctrlmode == WinEdge)
		grabresize(&(Arg){.i = MOVEZONE(sel, ev->x, ev->y) ? DragMove : DragSize});

	else if ((c = wintoclient(ev->window))) {
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		focus(c);
		restack(c, c->isfloating ? CliZoom : CliRaise);
	}
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (!c)
		return;
	if (cme->message_type == xatom[NetWMState]) {
		if (cme->data.l[1] == xatom[NetWMFullscreen]
		|| cme->data.l[2] == xatom[NetWMFullscreen])
			/* 1=_NET_WM_STATE_ADD, 2=_NET_WM_STATE_TOGGLE */
			setfullscreen(c, (cme->data.l[0] == 1
				|| (cme->data.l[0] == 2 && !c->isfullscreen)));
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		if (c->isfloating) {
			if (ev->value_mask & CWX)
				c->x = c->fx = ev->x;
			if (ev->value_mask & CWY)
				c->y = c->fy = ev->y;
			if (ev->value_mask & CWWidth)
				c->w = c->fw = ev->width;
			if (ev->value_mask & CWHeight)
				c->h = c->fw = ev->height;
			if ((ev->value_mask & (CWX|CWY))
			&& !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		if (ev->value_mask & CWSibling)
			wc.sibling = ev->above;
		if (ev->value_mask & CWStackMode)
			wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
}

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
}

/*
 * Handle raw presses, releases, mouse motion events,
 * and monitor changes.
 * This does not handle the keyboard shorcuts, but rather
 * the additional keyboard related functionaltily:
 *  - barshow (show bar raised when held down),
 *  - stackrelease (reorder window stack on key release)
 *  - ending resize or move actions on any key release.
 * See the keyproess function for keyboard shortcut handling.
 */
void
exthandler(XEvent *ev)
{
	/* first check mouse movement events to keep them snappy */
	if (ev->xcookie.evtype == XI_Motion) {
		motion();
		return;
	} else if (ev->xcookie.evtype == randroutputchange)
		updatemonitors();

	grabresizeabort();
	XGetEventData(dpy, &ev->xcookie);
	XIRawEvent *re = ev->xcookie.data;

	/* raise bar if trigger key is held down */
	if (ev->xcookie.evtype == XI_RawKeyPress && KCODE(barshow) == re->detail) {
		barfocus = 1;
		XRaiseWindow(dpy, barwin);
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, xatom[NetActiveWindow]);
	}
	else if (ev->xcookie.evtype == XI_RawKeyRelease) {
		/* lower bar if trigger key is released */
		if (KCODE(barshow) == re->detail) {
			barfocus = 0;
			if (sel)
				focus(sel);
			restack(NULL, CliRefresh);
		}
		/* zoom after cycling windows if releasing the modifier key, this gives
			 AltTab+Tab...select behavior like with common window managers */
		else if (ctrlmode == ZoomStack && KCODE(stackrelease) == re->detail) {
			restack(sel, CliZoom);
			arrange(); /* zooming tiled windows can rearrange tiling */
		}
	}
}

void
expose(XEvent *e)
{
	if (e->xexpose.count == 0)
		drawbar();
}

void
keypress(XEvent *e)
{
	for (int i = 0; i < keyslen; i++)
		if (e->xkey.keycode == KCODE(keys[i].key)
		&& KEYMASK(keys[i].mod) == KEYMASK(e->xkey.state))
			keys[i].func(&(keys[i].arg));
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e)
{
	int x, y, m;
	long state[] = { NormalState, None };
	Client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect
	|| wintoclient(ev->window))
		return;

	/**
	 * Manage the window by registering it as a new client
	 */
	if (!(c = calloc(1, sizeof(Client))))
		DIE("calloc failed.\n");
	attach(c);
	c->win = ev->window;
	c->isfloating = 1;
	c->tags = tagset;
	/* geometry */
	c->fx = wa.x;
	c->fy = wa.y;
	c->fw = wa.width;
	c->fh = wa.height;
	c->oldbw = wa.border_width;
	/* show window on same workspaces as its parent, if it has one */
	if (XGetTransientForHint(dpy, ev->window, &trans) && (t = wintoclient(trans)))
		c->tags = t->tags;

	/* find current monitor */
	if (MOUSEINF(dwin, x, y, dui))
		for (m = monslen-1; m > 0 && !INMON(x, y, mons[m]); m--);
	else m = 0;
	/* adjust to current monitor */
	if (c->fx + WIDTH(c) > mons[m].mx + mons[m].mw)
		c->fx = mons[m].mx + mons[m].mw - WIDTH(c);
	if (c->fy + HEIGHT(c) > mons[m].my + mons[m].mh)
		c->fy = mons[m].my + mons[m].mh - HEIGHT(c);
	c->fx = MAX(c->fx, mons[m].mx);
	c->bw = borderpx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, ev->window, CWBorderWidth, &wc);
	configure(c); /* propagates border_width, if size doesn't change */
	if (getatomprop(c, xatom[NetWMState]) == xatom[NetWMFullscreen])
		setfullscreen(c, 1);
	updatesizehints(c);
	XSelectInput(dpy, ev->window, PropertyChangeMask|StructureNotifyMask);
	PROPADD(Append, root, NetClientList, XA_WINDOW, 32, &c->win, 1);
	/* some windows require this */
	XMoveResizeWindow(dpy, c->win, c->fx + 2 * sw, c->fy, c->fw, c->fh);
	PROPSET(c->win, WMState, xatom[WMState], 32, state, 2);
	resize(c, c->fx, c->fy, c->fw, c->fh);
	restack(c, CliRaise);
	XMapWindow(dpy, c->win);
	focus(c);
}

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if (ev->state == PropertyDelete)
		return; /* ignore */
	else if ((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange();
			break;
		case XA_WM_NORMAL_HINTS:
			updatesizehints(c);
			break;
		}
		if (ev->atom == xatom[NetWMWindowType])
			if (getatomprop(c, xatom[NetWMState]) == xatom[NetWMFullscreen])
				setfullscreen(c, 1);
	}
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			XDeleteProperty(dpy, c->win, xatom[WMState]);
		else
			unmanage(c, 0);
	}
}

/***********************
* Config callable funcs
************************/

void
focusstack(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!sel)
		return;
	if (arg->i > 0) {
		for (c = sel->next; c && !ISVISIBLE(c); c = c->next);
		if (!c)
			for (c = clients; c && !ISVISIBLE(c); c = c->next);
	} else {
		for (i = clients; i != sel; i = i->next)
			if (ISVISIBLE(i))
				c = i;
		if (!c)
			for (; i; i = i->next)
				if (ISVISIBLE(i))
					c = i;
	}
	if (c) {
		focus(c);
		restack(c, CliRaise);
	}
}

void
grabresize(const Arg *arg) {
	/* abort if already in the desired mode */
	if (ctrlmode == arg->i)
		return;
	/* only grab if there is a selected window,
	   no support moving fullscreen or repositioning tiled windows. */
	if (!sel || sel->isfullscreen || (arg->i == DragMove && !sel->isfloating))
		return;

	/* set the drag mode so future motion applies to the action */
	ctrlmode = arg->i;
	/* detect if we should be dragging the tiled layout */
	if (ctrlmode == DragSize && !sel->isfloating)
		ctrlmode = DragTile;
	/* grab pointer and show resize cursor */
	XGrabPointer(dpy, root, True, ButtonPressMask,
		GrabModeAsync, GrabModeAsync,None,cursize,CurrentTime);
	if (ctrlmode != WinEdge)
		/* bring the window to the top */
		restack(sel, CliRaise);
}

void
grabstack(const Arg *arg)
{
	ctrlmode = ZoomStack;
	focusstack(arg);
}

void
killclient(const Arg *arg)
{
	if (sel && !sendevent(sel, xatom[WMDelete])) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void
pin(const Arg *arg)
{
	restack(sel, CliPin);
}

void
quit(const Arg *arg)
{
	end = 1;
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execvp((*(char***)arg->v)[0], *(char ***)arg->v);
		fprintf(stderr, "filetwm: execvp %s", (*(char ***)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
}

void
tag(const Arg *arg)
{
	if (sel && arg->ui & TAGMASK) {
		sel->tags = arg->ui & TAGMASK;
		arrange();
	}
}

void
togglefloating(const Arg *arg)
{
	if (!sel)
		return;
	if (sel->isfullscreen)
		setfullscreen(sel, 0);
	if ((sel->isfloating = !sel->isfloating))
		resize(sel, sel->fx, sel->fy, sel->fw, sel->fh);
	arrange();
}

void
togglefullscreen(const Arg *arg)
{
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
}

/**
 * Toogle (add/remove) window to specified workspace(s),
 * ensuring the window always remains on at least one workspace.
 * @arg: contains ui parameter with a bitmask indicating the
 *       workspace numbers to toggle for.
 */
void
toggletag(const Arg *arg)
{
	if (sel && sel->tags ^ (arg->ui & TAGMASK)) {
		sel->tags = sel->tags ^ (arg->ui & TAGMASK);
		arrange();
	}
}

void
view(const Arg *arg)
{
	tagset = arg->ui & TAGMASK;
	drawbar();
	arrange();
}

void
viewshift(const Arg *arg)
{
	tagset = TAGSHIFT(tagset, arg->i);
	drawbar();
	arrange();
}

void
viewtagshift(const Arg *arg)
{
	if (sel) sel->tags = TAGSHIFT(sel->tags, arg->i);
	viewshift(arg);
}

void
zoom(const Arg *arg)
{
	restack(sel, CliZoom);
	arrange(); /* zooming tiled windows can rearrange tiling */
}

/***********************
* Core execution code
************************/

void
setup(void)
{
	int screen, xre;
	unsigned char xi[XIMaskLen(XI_LASTEVENT)] = {0};
	XIEventMask evm;
	Atom utf8string;
	void (*conf)(void);

	/* register handler to clean up any zombies immediately */
	sigchld(0);

	/* Load configs.
	   First load the default included config.
	   Then load the distribution's config plugin, if one exists.
	   Then load the user's config plugin, if they have one.
	   Leave the current working directory in the user's home dir. */
	defaultconfig();
	if (LOADCONF("/etc/config/filetwmconf.so", conf))
		conf();
	if (!chdir(getenv("HOME")) && LOADCONF(".config/filetwmconf.so", conf))
		conf();
	else if (access(".config/filetwmconf.so", F_OK) == 0)
		DIE(dlerror());

	/* init screen and display */
	XSetErrorHandler(xerror);
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drawable = XCreatePixmap(dpy, root, sw, sh, DefaultDepth(dpy, screen));
	drawablexft = XftDrawCreate(dpy, drawable, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen));
	gc = XCreateGC(dpy, root, 0, NULL);
	XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
	if (!(xfont = XftFontOpenName(dpy, screen, font)))
		DIE("font couldn't be loaded.\n");
	/* init monitor layout */
	if (MONNULL(mons[0])) {
		updatemonitors();
		/* select xrandr events (if monitor layout isn't hard configured) */
		if (XRRQueryExtension(dpy, &xre, &di)) {
			randroutputchange = xre + RRNotify_OutputChange;
			XRRSelectInput(dpy, root, RROutputChangeNotifyMask);
		}
	}
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	xatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	xatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	xatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	xatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	xatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	xatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	xatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	xatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	xatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	xatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	xatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	xatom[NetWMWinDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	xatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	xatom[NetCliStack] = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", False);
	/* init cursors */
	curpoint = XCreateFontCursor(dpy, XC_left_ptr);
	cursize = XCreateFontCursor(dpy, XC_sizing);
	XDefineCursor(dpy, root, curpoint);
	/* init colors */
	for (int i = 0; i < colslen; i++)
		if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
				DefaultColormap(dpy, screen), colors[i], &cols[i]))
			DIE("error, cannot allocate colors.\n");
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	PROPSET(wmcheckwin, NetWMCheck, XA_WINDOW, 32, &wmcheckwin, 1);
	PROPSET(wmcheckwin, NetWMName, utf8string, 8, "filetwm", 3);
	PROPSET(root, NetWMCheck, XA_WINDOW, 32, &wmcheckwin, 1);
	/* EWMH support per view */
	PROPSET(root, NetSupported, XA_ATOM, 32, xatom, NetLast);
	XDeleteProperty(dpy, root, xatom[NetClientList]);
	/* init bar */
	barwin = XCreateWindow(dpy, root, barpos[0], barpos[1], barpos[2], BARH, 0,
		DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
		CWOverrideRedirect|CWBackPixmap|CWEventMask, &(XSetWindowAttributes){
			.override_redirect = True,
			.background_pixmap = ParentRelative,
			.event_mask = ButtonPressMask|ExposureMask});
	XMapRaised(dpy, barwin);
	XSetClassHint(dpy, barwin, &(XClassHint){"filetwm", "filetwm"});
	updatestatus();
	/* select events */
	XSelectInput(dpy, root, SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|StructureNotifyMask|PropertyChangeMask);
	/* prepare motion capture */
	motion();
	/* select xinput events */
	if (XQueryExtension(dpy, "XInputExtension", &di, &di, &di)
	&& XIQueryVersion(dpy, &(int){2}, &(int){0}) == Success) {
		XISetMask(xi, XI_Motion);
		XISetMask(xi, XI_ButtonRelease);
		XISetMask(xi, XI_RawKeyRelease);
		XISetMask(xi, XI_RawKeyPress);
		evm.deviceid = XIAllMasterDevices;
		evm.mask_len = sizeof(xi);
		evm.mask = xi;
		XISelectEvents(dpy, root, &evm, 1);
	}
	grabkeys();
	focus(NULL);
}

int
main(int argc, char *argv[])
{
	XEvent ev;

	if (argc == 2 && !strcmp("-v", argv[1]))
		DIE("filetwm-"VERSION"\n");
	if (argc != 1)
		DIE("usage: filetwm [-v]\n");
	if (!(dpy = XOpenDisplay(NULL)))
		DIE("filetwm: cannot open display.\n");

	setup();

	/* main event loop */
	XSync(dpy, False);
	while (!end && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */

	/* cleanup */
	view(&(Arg){.ui = ~0});
	while (clients)
		unmanage(clients, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	XUnmapWindow(dpy, barwin);
	XDestroyWindow(dpy, barwin);
	XFreeCursor(dpy, curpoint);
	XFreeCursor(dpy, cursize);
	XDestroyWindow(dpy, wmcheckwin);
	XftFontClose(dpy, xfont);
	XFreePixmap(dpy, drawable);
	XFreeGC(dpy, gc);
	XftDrawDestroy(drawablexft);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, xatom[NetActiveWindow]);
	XCloseDisplay(dpy);

	return EXIT_SUCCESS;
}
