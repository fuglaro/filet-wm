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
#define BARZONE(X, Y) (topbar ? Y <= mons->my : Y >= mons->my + mons->mh - 1\
	&& (X >= mons->mx) && (X <= mons->mx + mons->mw))
#define INZONE(C, X, Y) (X >= C->x - C->bw && Y >= C->y - C->bw\
	&& X <= C->x + WIDTH(C) + C->bw && Y <= C->y + HEIGHT(C) + C->bw)
#define MOVEZONE(C, X, Y) (INZONE(C, X, Y)\
	&& (abs(C->x - X) <= C->bw || abs(C->y - Y) <= C->bw))
#define RESIZEZONE(C, X, Y) (INZONE(C, X, Y)\
	&& (abs(C->x + WIDTH(C) - X) <= C->bw || abs(C->y + HEIGHT(C) - Y) <= C->bw))

/* monitor macros */
#define BARH (TEXTPAD + 2)
#define BARY (topbar ? mons->my : mons->my + WINH(mons[0]))
#define INMON(X, Y, M)\
	(X >= M.mx && X < M.mx + M.mw && Y >= M.my && Y < M.my + M.mh)
#define MONNULL(M) (M.mx == 0 && M.my == 0 && M.mw == 0 && M.mh == 0)
#define SETMON(M, R) {M.mx = R.x; M.my = R.y; M.mw = R.width; M.mh = R.height;}
#define WINH(M) (&M == mons ? M.mh - BARH : M.mh)
#define WINY(M) (&M == mons && topbar ? M.my + BARH : M.my)
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
enum { ClkLauncher, ClkWinTitle, ClkStatus, ClkTagBar, ClkLast };
/* mouse motion modes */
enum { DragMove, DragSize, DragTile, DragCheck, DragNone };
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
	char name[256], zenname[256];
	float mina, maxa;
	int x, y, w, h;
	int fx, fy, fw, fh; /*remember during tiled and fullscreen states */
	int basew, baseh, maxw, maxh, minw, minh;
	int bw, fbw, oldbw;
	unsigned int tags;
	int isfloating, isurgent, fstate, isfullscreen;
	Client *next;
	Window win;
	Time zenping;
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
static void buttonrelease(XEvent *e);
static void clientmessage(XEvent *e);
static void configurerequest(XEvent *e);
static void destroynotify(XEvent *e);
static void expose(XEvent *e);
static void exthandler(XEvent *ev);
static void focus(Client *c);
static void keypress(XEvent *e);
static void keyrelease(XEvent *e);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void propertynotify(XEvent *e);
static void unmapnotify(XEvent *e);

/* variables */
static char stxt[256] = { /* status text */
	'F','i','l','e','t','L','i','g','n','u','x','\0',[255]='\0'};
static int sw, sh;           /* X display screen geometry width, height */
static Window barwin;        /* the topbar */
static int barfocus;         /* when the topbar is forced raised */
static int dragmode = DragNone; /* mouse mode (resize/repos/arrange/etc) */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int tagset = 1; /* mask for which workspaces are displayed */
/* The event handlers are organized in an array which is accessed
 * whenever a new event has been fetched. The array indicies associate
 * with event numbers. This allows event dispatching in O(1) time.
 */
static void (*handler[LASTEvent]) (XEvent *) = { /* XEvent callbacks */
	[ButtonPress] = buttonpress,
	[ButtonRelease] = buttonrelease,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[DestroyNotify] = destroynotify,
	[Expose] = expose,
	[GenericEvent] = exthandler,
	[KeyPress] = keypress,
	[KeyRelease] = keyrelease,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify,
};
static int randroutputchange;     /* holds event-type: monitor change */
static Atom xatom[XAtomLast];     /* holds X types */
static int end;                   /* end session trigger (quit) */
static Display *dpy;              /* X session display reference */
static Drawable drawable;         /* canvas for drawing (topbar) */
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
/* single value config assignment */
#define S(T, N, V) N = V /* compatibily for plugin-flavor macro */
/* variable length config assignment */
#define V(T, N, L, ...) do {static T _##N[] = __VA_ARGS__; N = _##N; L} while(0)
/* pointer array config assignment */
#define P(T, N, ...) V(T,N,,__VA_ARGS__;)
/* array of fixed length config assignment */
#define A(T, N, ...) V(T,N,N##len = (sizeof _##N/sizeof _##N[0]);,__VA_ARGS__;)

/* configurable values (see defaultconfig) */
Monitor *mons;
char *lsymbol, *font, **colors, **tags, **launcher, **terminal, **upvol,
	**downvol, **mutevol, **suspend, **dimup, **dimdown, **help;
int borderpx, snap, topbar, zenmode, tagslen, monslen, *nmain, keyslen,
	buttonslen;
float *mfact;
KeySym stackrelease;
Key *keys;
Button *buttons;

void
defaultconfig(void)
{
	/* appearance */
	S(int, borderpx, 1); /* border pixel width of windows */
	S(int, snap, 8); /* edge snap pixel distance */
	S(int, topbar, 1); /* 0 means bottom bar */
	S(int, zenmode, 3); /* ignores showing rapid client name changes (seconds) */
	S(char*, lsymbol, ">"); /* launcher symbol */
	S(char*, font, "monospace:size=8");
	/* colors (must be five colors: fg, bg, highlight, border, sel-border) */
	P(char*, colors, { "#dddddd", "#111111", "#335577", "#555555", "#dd4422" });

	/* virtual workspaces (must be 32 or less, *usually*) */
	A(char*, tags, { "1", "2", "3", "4", "5", "6", "7", "8", "9" });

	/* monitor layout
	   Set mons to the number of monitors you want supported.
	   Initialise with {0} for autodetection of monitors,
	   otherwise set the position and size ({x,y,w,h}).
	   The first monitor will be the primary monitor and have the bar.
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
	/* factor of main area size [0.05..0.95] (for each monitor) */
	P(float, mfact, {0.6});
	/* number of clients in main area (for each monitor) */
	P(int, nmain, {1});

	/* commands */
	P(char*, launcher, { "dmenu_run", "-p", ">", "-m", "0", "-i", "-fn",
		"monospace:size=8", "-nf", "#dddddd", "-sf", "#dddddd", "-nb", "#111111",
		"-sb", "#335577", NULL });
	P(char*, terminal, { "st", NULL });
	#define VOLCMD(A) ("amixer -q set Master "#A"; xsetroot -name \"Volume: "\
		"$(amixer sget Master | awk -F'[][]' '/dB/ { print $2, $6 }')\"")
	P(char*, upvol, { "bash", "-c", VOLCMD("5%+"), NULL });
	P(char*, downvol, { "bash", "-c", VOLCMD("5%-"), NULL });
	P(char*, mutevol, { "bash", "-c", VOLCMD("toggle"), NULL });
	P(char*, suspend, {
		"bash", "-c", "killall slock; slock systemctl suspend -i", NULL });
	#define DIMCMD(A) ("xbacklight "#A" 5; xsetroot -name \"Brightness: "\
		"$(xbacklight | cut -d. -f1)%\"")
	P(char*, dimup, { "bash", "-c", DIMCMD("-inc"), NULL });
	P(char*, dimdown, { "bash", "-c", DIMCMD("-dec"), NULL });
	P(char*, help, { "st", "-t", "Help", "-e", "bash", "-c",
		"man filetwm || man -l ~/.config/filetwmconf.1", NULL });

	/* keyboard shortcut definitions */
	#define AltMask Mod1Mask
	#define WinMask Mod4Mask
	#define TK(KEY) { WinMask, XK_##KEY, view, {.ui = 1 << (KEY - 1)} }, \
	{       WinMask|ShiftMask, XK_##KEY, tag, {.ui = 1 << (KEY - 1)} }, \
	{                 AltMask, XK_##KEY, toggletag, {.ui = 1 << (KEY - 1)} },
	/* Alt+Tab style behaviour key release */
	S(KeySym, stackrelease, XK_Alt_L);
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
		{ ClkLauncher, Button1, spawn, {.v = &launcher } },
		{ ClkWinTitle, Button1, focusstack, {.i = +1 } },
		{ ClkWinTitle, Button3, focusstack, {.i = -1 } },
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

void
attach(Client *c)
{
	c->next = clients;
	clients = c;
}

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

void
detach(Client *c)
{
	Client **tc;

	for (tc = &clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

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
	y = MAX(1 + (topbar?BARH:0) - h - 2*c->bw, MIN(sh - 1 - (topbar?0:BARH), y));

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
		y = (abs(WINY(mons[m1]) - y) < snap) ? WINY(mons[m1]) : y;
		/* snap size */
		if (abs((mons[m2].mx + mons[m2].mw) - (x + w + 2*c->bw)) < snap)
			w = mons[m2].mx + mons[m2].mw - x - 2*c->bw;
		if (abs((WINY(mons[m2]) + WINH(mons[m2])) - (y + h + 2*c->bw)) < snap)
			h = WINY(mons[m2]) + WINH(mons[m2]) - y - 2*c->bw;
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
 * Sets the urgency of the given client.
 * Call this function to set the status to urgent, but
 * if the given client is focussed, this will have the opposite
 * effect and will ensure the urgent status is unset.
 */
void
seturgency(Client *c)
{
	XWMHints *wmh;

	if (!c || c->isurgent == (sel != c) || !(wmh = XGetWMHints(dpy, c->win)))
		return;
	c->isurgent = (sel != c);
	/* update the client's hint property with resulting urgency status */
	wmh->flags = c->isurgent?(wmh->flags|XUrgencyHint):(wmh->flags&~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

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
 * Retrieve the current client window name by first checking the
 * legacy XA_WM_NAME property on the window and then overriding
 * with the EWMH compliant property if one exists.
 * If neither are present, the name is set to an empty string.
 */
void
updateclientname(Client *c) {
	char **v = NULL;
	XTextProperty p;

	/* set to empty string and protect from upcoming overflows */
	c->name[0] = c->name[sizeof(c->name) - 1] = '\0';

	/* retrieve legacy name property */
	if (XGetTextProperty(dpy, c->win, &p, XA_WM_NAME) && p.nitems) {
		if (XmbTextPropertyToTextList(dpy, &p, &v, &di) >= Success && *v) {
			strncpy(c->name, *v, sizeof(c->name) - 1);
			XFreeStringList(v);
		}
		XFree(p.value);
	}

	/* retrieve and override name from EWMH supporting clients */
	if (XGetTextProperty(dpy, c->win, &p, xatom[NetWMName]) && p.nitems) {
		strncpy(c->name, (char *)p.value, sizeof(c->name) - 1);
		XFree(p.value);
	}
}

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

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
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
	unsigned int m, h, mw;
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
				h = (WINH(mons[m]) - my[m]) / (MIN(nm[m], nmain[m]) - i[m]);
				resize(c, mons[m].mx, WINY(mons[m]) + my[m], mw-(2*c->bw), h-(2*c->bw));
				if (my[m] + HEIGHT(c) < WINH(mons[m]))
					my[m] += HEIGHT(c);
			} else {
				h = (WINH(mons[m]) - ty[m]) / (nm[m] - i[m]);
				resize(c, mons[m].mx + mw, WINY(mons[m]) + ty[m],
					mons[m].mw - mw - (2*c->bw), h - (2*c->bw));
				if (ty[m] + HEIGHT(c) < WINH(mons[m]))
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
drawtopbar(int zen)
{
	int i, x, w;
	int boxs = TEXTPAD / 9;
	int boxw = TEXTPAD / 6 + 2;
	unsigned int occ = 0, urg = 0;
	Client *c;

	/* get urgency and occupied status for each tag */
	for (c = clients; c; c = c->next) {
		occ |= c->tags;
		urg |= c->tags * c->isurgent;
	}

	/* draw launcher button (left align) */
	drawtext(0, 0, (x = TEXTW(lsymbol)), BARH, lsymbol, &cols[fg], &cols[mark]);

	/* draw window title (left align) */
	drawtext(x, 0, mons->mw - x, BARH,
		sel ? (zen ? sel->zenname : sel->name) : "", &cols[fg], &cols[bg]);

	/* draw tags (right align) */
	x = mons->mw;
	for (i = tagslen - 1; i >= 0; i--) {
		x -= (w = TEXTW(tags[i]));
		drawtext(x, 0, w, BARH, tags[i], &cols[urg & 1 << i ? bg : fg],
			&cols[urg & 1 << i ? fg : (tagset & 1 << i ? mark : bg)]);
		if (occ & 1 << i) {
			XSetForeground(dpy, gc, cols[fg].pixel);
			XFillRectangle(dpy, drawable, gc, x + boxs, boxs, boxw, boxw);
		}
	}

	/* draw status (right align) */
	w = TEXTW(stxt);
	drawtext(x - w, 0, w, BARH, stxt, &cols[fg], &cols[bg]);

	/* display composited bar */
	XCopyArea(dpy, drawable, barwin, gc, 0, 0, mons->mw, BARH, 0, 0);
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
	seturgency(c); /* clear urgency if set */
	drawtopbar(0);
}

void
grabkeys(void)
{
	unsigned int i, j;
	/* NumLock assumed to be Mod2Mask */
	unsigned int mods[] = { 0, LockMask, Mod2Mask, Mod2Mask|LockMask };

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (i = 0; i < keyslen; i++)
		for (j = 0; j < (sizeof mods / sizeof mods[0]) && KCODE(keys[i].key); j++)
			XGrabKey(dpy, KCODE(keys[i].key), keys[i].mod | mods[j], root,
				True, GrabModeAsync, GrabModeAsync);
}

void
grabresizeabort()
{
	int i, m;
	char keystatemap[32];

	if (dragmode == DragNone)
		return;
	/* only release grab when all keys are up (or key repeat would interfere) */
	XQueryKeymap(dpy, keystatemap);
	for (i = 0; i < 32; i++)
		if (keystatemap[i] && dragmode != DragCheck)
			return;

	/* update the monitor layout to match any tiling changes */
	if (sel && dragmode == DragTile) {
		for (m = 0; m < monslen-1 && !INMON(sel->x, sel->y, mons[m]); m++);
		mfact[m] = MIN(0.95, MAX(0.05, (float)WIDTH(sel) / mons[m].mw));
		nmain[m] = MAX(1, mons[m].mh / HEIGHT(sel));
		arrange();
	}

	/* release the drag */
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	dragmode = DragNone;
}

void
rawmotion()
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
	if (sel && dragmode == DragMove)
		resize(sel, sel->fx + x, sel->fy + y, sel->fw, sel->fh);
	if (sel && dragmode == DragSize)
		resize(sel, sel->fx, sel->fy, MAX(sel->fw + x, 1), MAX(sel->fh + y, 1));
	if (sel && dragmode == DragTile)
		resize(sel, sel->x, sel->y, MAX(sel->w + x, 1), MAX(sel->h + y, 1));
	if (dragmode == DragCheck && (!sel || BARZONE(rx, ry)
		|| (!MOVEZONE(sel, rx, ry) && !RESIZEZONE(sel, rx, ry))))
		grabresizeabort();
	if (dragmode != DragNone)
		return;

	/* top bar raise when mouse hits the screen edge.
	   especially useful for apps that capture the keyboard. */
	if (BARZONE(rx, ry) && !barfocus) {
		barfocus = 1;
		XRaiseWindow(dpy, barwin);
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, xatom[NetActiveWindow]);
	} else if (!BARZONE(rx, ry) && barfocus) {
		barfocus = 0;
		if (sel)
			focus(sel);
		restack(NULL, CliRefresh);
	}

	c = cw != lastcw ? wintoclient(cw) : c;
	lastcw = cw;
	/* focus follows mouse */
	if (c && c != sel)
		focus(c);
	/* watch for border edge locations for resizing */
	if (c && !mask && (MOVEZONE(c, rx, ry) || RESIZEZONE(c, rx, ry)))
		grabresize(&(Arg){.i = DragCheck});
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
	drawtopbar(1);
}

/***********************
* Event handler funcs
************************/

void
buttonpress(XEvent *e)
{
	int i, x = mons->mw, click = ClkWinTitle;
	Client *c;
	Arg arg = {0};
	XButtonPressedEvent *ev = &e->xbutton;

	if (ev->window == barwin) {
		for (i = tagslen - 1; i >= 0 && ev->x < (x -= TEXTW(tags[i])); i--);
		if (i >= 0) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		} else if (ev->x > x - TEXTW(stxt))
			click = ClkStatus;
		else if (ev->x < TEXTW(lsymbol))
			click = ClkLauncher;
		for (i = 0; i < buttonslen; i++)
			if (click == buttons[i].click && buttons[i].button == ev->button)
				buttons[i].func(arg.ui ? &arg : &buttons[i].arg);
	}

	else if (sel && dragmode == DragCheck)
		grabresize(&(Arg){.i = MOVEZONE(sel, ev->x, ev->y) ? DragMove : DragSize});

	else if ((c = wintoclient(ev->window))) {
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		focus(c);
		restack(c, c->isfloating ? CliZoom : CliRaise);
	}
}

void
buttonrelease(XEvent *e)
{
	grabresizeabort();
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
	} else if (cme->message_type == xatom[NetActiveWindow])
			seturgency(c);
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

void
exthandler(XEvent *ev)
{
	if (ev->xcookie.evtype == XI_RawMotion)
		rawmotion();
	else if (ev->xcookie.evtype == randroutputchange)
		updatemonitors(ev);
}

void
expose(XEvent *e)
{
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0)
		drawtopbar(0);
}

void
keypress(XEvent *e)
{
	unsigned int i;

	grabresizeabort();
	for (i = 0; i < keyslen; i++)
		if (e->xkey.keycode == KCODE(keys[i].key)
		&& KEYMASK(keys[i].mod) == KEYMASK(e->xkey.state))
			keys[i].func(&(keys[i].arg));
}

void
keyrelease(XEvent *e)
{
	grabresizeabort();
	/* zoom after cycling windows if releasing the modifier key, this gives
	   Alt+Tab+Tab... behavior like with common window managers */
	if (dragmode == DragNone && KCODE(stackrelease) == e->xkey.keycode) {
		restack(sel, CliZoom);
		arrange(); /* zooming tiled windows can rearrange tiling */
		XUngrabKeyboard(dpy, CurrentTime);
	}
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
	c->zenping = 0;
	c->isfloating = 1;
	c->tags = tagset;
	/* geometry */
	c->fx = wa.x;
	c->fy = wa.y;
	c->fw = wa.width;
	c->fh = wa.height;
	c->oldbw = wa.border_width;
	/* retrieve the window title */
	updateclientname(c);
	strcpy(c->zenname, c->name);
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
	/* only fix client y-offset, if the client center might cover the bar */
	c->fy = MAX(c->fy, ((BARY == mons->my) && (c->fx + (c->fw / 2) >= mons->mx)
		&& (c->fx + (c->fw / 2) < mons->mx + mons->mw)) ? BARH : mons->my);
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
	XWMHints *wmh;

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
		case XA_WM_HINTS:
			if ((wmh = XGetWMHints(dpy, c->win))) {
				c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
				seturgency(c); /* clear urgency if client is focussed */
				XFree(wmh);
				drawtopbar(1);
			}
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == xatom[NetWMName]) {
			/* retrieve the window title */
			updateclientname(c);
			if (c == sel && ev->time - c->zenping > zenmode * 1000)
				strcpy(c->zenname, c->name);
			c->zenping = ev->time;
			drawtopbar(1);
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
	if (dragmode == arg->i)
		return;
	/* only grab if there is a selected window,
	   no support moving fullscreen or repositioning tiled windows. */
	if (!sel || sel->isfullscreen || (arg->i == DragMove && !sel->isfloating))
		return;

	/* set the drag mode so future motion applies to the action */
	dragmode = arg->i;
	/* detect if we should be dragging the tiled layout */
	if (dragmode == DragSize && !sel->isfloating)
		dragmode = DragTile;
	/* capture input */
	XGrabPointer(dpy, root, True, ButtonPressMask|ButtonReleaseMask
		|PointerMotionMask, GrabModeAsync, GrabModeAsync,None,cursize,CurrentTime);
	if (dragmode != DragCheck) {
		XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
		/* bring the window to the top */
		restack(sel, CliRaise);
	}
}

void
grabstack(const Arg *arg)
{
	XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
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
	arrange();
}

void
viewshift(const Arg *arg)
{
	tagset = TAGSHIFT(tagset, arg->i);
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
	int i, screen, xre;
	unsigned char xi[XIMaskLen(XI_RawMotion)] = { 0 };
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
	for (i = 0; i < colslen; i++)
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
	/* init topbar */
	barwin = XCreateWindow(dpy, root, mons->mx, BARY, mons->mw, BARH, 0,
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
		|ButtonPressMask|ButtonReleaseMask|StructureNotifyMask|PropertyChangeMask);
	/* prepare motion capture */
	rawmotion();
	/* select xinput events */
	if (XQueryExtension(dpy, "XInputExtension", &di, &di, &di)
	&& XIQueryVersion(dpy, &(int){2}, &(int){0}) == Success) {
		XISetMask(xi, XI_RawMotion);
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
	XUngrabKeyboard(dpy, CurrentTime);
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
