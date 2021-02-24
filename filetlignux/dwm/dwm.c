/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK           (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)      (mask & ~(numlockmask|LockMask)\
                              & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask\
                                 |Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)\
                   (MAX(0, MIN((x)+(w),(m)->mx+(m)->mw) - MAX((x),(m)->mx))\
                    * MAX(0, MIN((y)+(h),(m)->my+(m)->mh) - MAX((y),(m)->my)))
#define ISVISIBLE(C)            ((C->tags & tagset[seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define MOVEZONE(C, X, Y)   (abs(C->x - X) <= C->bw || abs(C->y - Y) <= C->bw)
#define RESIZEZONE(C, X, Y)     (abs(C->x + WIDTH(C) - X) <= C->bw ||\
                                 abs(C->y + HEIGHT(C) - Y) <= C->bw)
#define WINY(M)                 (&M == mons && topbar ? M.my + bh : M.my)
#define WINH(M)                 (&M == mons ? M.mh - bh : M.mh)
#define MONNULL(M)          (M.mx == 0 && M.my == 0 && M.mw == 0 && M.mh == 0)
#define SETMON(M, R)            {M.mx = R.x; M.my = R.y;\
                                 M.mw = R.width; M.mh = R.height;}
#define INMON(X, Y, M)          (X >= M.mx && X < M.mx + M.mw &&\
                                 Y >= M.my && Y < M.my + M.mh)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast };
                                                           /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */
enum { DragMove, DragSize };

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int fx, fy, fw, fh; /*remember during tiled and fullscreen states */
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	unsigned int tags;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	Client *next;
	Window win;
	Time zenping;
	char zenname[256];
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct Monitor Monitor;
struct Monitor {
	int mx, my, mw, mh;   /* screen size */
};

/* function declarations */
static int applysizehints(Client *c, int *x, int *y, int *w, int *h);
static void arrange(void);
static void attach(Client *c);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurerequest(XEvent *e);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void drawbar(int zen);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void exthandler(XEvent *ev);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusstack(const Arg *arg);
static void focusview(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c);
static void grabkeys(void);
static void grabresize(const Arg *arg);
static void grabstack(const Arg *arg);
static void keypress(XEvent *e);
static void keyrelease(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void moveview(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static void rawmotion(XEvent *e);
static void resize(Client *c, int x, int y, int w, int h);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void restack(void);
static void run(void);
static void scan(void);
static int sendevent(Client *c, Atom proto);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setup(void);
static void seturgent(Client *c, int urg);
unsigned int shiftviews(unsigned int v, int i);
static void showhide(Client *c);
static void sigchld(int unused);
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static void tile(void);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebars(void);
static void updateclientlist(void);
static void updatemonitors(XEvent *e);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static Window barwin;
static int bh, blw, by, vw = 0;  /* bar geometry */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static int stackgrabbed = 0;
static int barfocus = 0;
static Window *lastraised = NULL;
unsigned int seltags;
unsigned int tagset[2];
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[GenericEvent] = exthandler,
	[KeyPress] = keypress,
	[KeyRelease] = keyrelease,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify,
	[XI_RawMotion] = rawmotion,
};
static Atom wmatom[WMLast], netatom[NetLast];
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Client *clients;
static Client *sel;
static Client *raised;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

int
applysizehints(Client *c, int *x, int *y, int *w, int *h)
{
	int baseismin;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	{
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	}
	if (*h < bh)
		*h = bh;
	if (*w < bh)
		*w = bh;
	if (resizehints || c->isfloating) {
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if (c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw)
			*w -= *w % c->incw;
		if (c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(void)
{
	int di;
	unsigned int dui;
	Window dummy, cw;
	Client *c;

	showhide(clients);
	tile();
	restack();
	XQueryPointer(dpy, root, &dummy, &cw, &di, &di, &di, &di, &dui);
	if (cw && (c = wintoclient(cw)))
		grabbuttons(c);
	else if (sel)
		grabbuttons(sel);
}

void
attach(Client *c)
{
	c->next = clients;
	clients = c;
}

void
buttonpress(XEvent *e)
{
	unsigned int i, x, click;
	Arg arg = {0};
	Client *c;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClkRootWin;
	if (ev->window == barwin) {
		i = 0;
		x = vw;
		do
			x += TEXTW(tags[i]);
			while (ev->x >= vw && ev->x >= x && ++i < LENGTH(tags));
			if (ev->x >= vw && i < LENGTH(tags)) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		} else if (ev->x < blw)
			click = ClkLtSymbol;
		else if (ev->x > vw - (int)TEXTW(stext))
			click = ClkStatusText;
		else
			click = ClkWinTitle;
	} else if ((c = wintoclient(ev->window))) {
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		if (c->isfloating) pop(c);
		raised = c;
		focus(c);
		restack();
		click = ClkClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanup(void)
{
	Arg a = {.ui = ~0};
	size_t i;

	view(&a);
	while (clients)
		unmanage(clients, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	XUngrabKeyboard(dpy, CurrentTime);
	XUnmapWindow(dpy, barwin);
	XDestroyWindow(dpy, barwin);
	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		free(scheme[i]);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (c != sel && !c->isurgent)
			seturgent(c, 1);
	}
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
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
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
	XSync(dpy, False);
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
detach(Client *c)
{
	Client **tc;

	for (tc = &clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
drawbar(int zen)
{
	int x, w, tw = 0;
	int boxs = drw->fonts->h / 9;
	int boxw = drw->fonts->h / 6 + 2;
	unsigned int i, occ = 0, urg = 0;
	Client *c;

	vw = mons->mw;
	for (i = 0; i < LENGTH(tags); i++) vw -= TEXTW(tags[i]);

	/* draw status first so it can be overdrawn by tags later */
	drw_setscheme(drw, scheme[SchemeNorm]);
	tw = TEXTW(stext) - lrpad + 2; /* 2px right padding */
	drw_text(drw, vw - tw, 0, tw, bh, 0, stext, 0);

	for (c = clients; c; c = c->next) {
		occ |= c->tags;
		if (c->isurgent)
			urg |= c->tags;
	}
	x = vw;
	for (i = 0; i < LENGTH(tags); i++) {
		w = TEXTW(tags[i]);
		drw_setscheme(drw, scheme[tagset[seltags] & 1 << i ? SchemeSel : SchemeNorm]);
		drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
		if (occ & 1 << i)
			drw_rect(drw, x + boxs, boxs, boxw, boxw,
				sel && sel->tags & 1 << i,
				urg & 1 << i);
		x += w;
	}

	x = 0;
	w = blw = TEXTW(lsymbol);
	drw_setscheme(drw, scheme[SchemeSel]);
	x = drw_text(drw, x, 0, w, bh, lrpad / 2, lsymbol, 0);

	if ((w = vw - x - tw) > bh) {
		if (sel) {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, x, 0, w, bh, lrpad / 2, zen ? sel->zenname : sel->name, 0);
			if (sel->isfloating)
				drw_rect(drw, x + boxs, boxs, boxw, boxw, sel->isfixed, 0);
		} else {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_rect(drw, x, 0, w, bh, 1, 1);
		}
	}
	drw_map(drw, barwin, 0, 0, mons->mw, bh);
}

void
enternotify(XEvent *e)
{
	Client *c;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	if ((c = wintoclient(ev->window)) != sel)
		focus(c);
}

void
exthandler(XEvent *ev)
{
	if (handler[ev->xcookie.evtype])
		handler[ev->xcookie.evtype](ev); /* call handler */
}

void
expose(XEvent *e)
{
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0)
		drawbar(0);
}

void
focus(Client *c)
{
	if (!c || !ISVISIBLE(c)){
		if (sel)
			for (c = sel; c && !ISVISIBLE(c); c = c->next);
		for (c = clients; c && !ISVISIBLE(c); c = c->next);
	}
	if (sel && sel != c)
		unfocus(sel, 0);
	if (c) {
		if (c->isurgent)
			seturgent(c, 0);
		grabbuttons(c);
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
		setfocus(c);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
		raised = c;
	}
	sel = c;
	drawbar(0);
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (sel && ev->window != sel->win)
		setfocus(sel);
}

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
		restack();
		grabbuttons(c);
	}
}

void
focusview(const Arg *arg)
{
	tagset[seltags] = shiftviews(tagset[seltags], arg->i);
	focus(NULL);
	arrange();
}

Atom
getatomprop(Client *c, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING)
		strncpy(text, (char *)name.value, size - 1);
	else {
		if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
			strncpy(text, *list, size - 1);
			XFreeStringList(list);
		}
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *c)
{
	updatenumlockmask();
	{
		int di;
		unsigned int i, j, dui;
		unsigned int modifiers[] = {
			0, LockMask, numlockmask, numlockmask|LockMask };
		Window cw, dummy;
		Client *cwc = NULL;

		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);

		XQueryPointer(dpy, root, &dummy, &cw, &di, &di, &di, &di, &dui);
		if (cw && (cwc = wintoclient(cw)))
			if ((cwc != raised) || (cwc->isfloating && cwc != clients))
				XGrabButton(dpy, AnyButton, AnyModifier, cwc->win, False,
					BUTTONMASK, GrabModeSync, GrabModeSync, None, None);

		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = {
			0, LockMask, numlockmask, numlockmask|LockMask };
		KeyCode code;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		for (i = 0; i < LENGTH(keys); i++)
			if ((code = XKeysymToKeycode(dpy, keys[i].keysym)))
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
						True, GrabModeAsync, GrabModeAsync);
	}
}

void
grabresize(const Arg *arg) {
	static int grabguard = 0;
	int x, y, i, m1, m2, type = arg->i;
	char keydown = 'x';
	char keystatemap[32];
	Client *c;
	Client oc, nc;
	XEvent ev;
	Time lasttime = 0;

	if (grabguard)
		return;
	if (!(c = sel))
		return;
	if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack();
	nc = oc = *c;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[type == DragMove ? CurMove : CurResize]->cursor, CurrentTime)
		!= GrabSuccess)
		return;
	XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
	if (!getrootptr(&x, &y))
		return;

	grabguard = 1;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask|
			KeyPressMask|KeyReleaseMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case KeyRelease:
			XQueryKeymap(dpy, keystatemap);
			keydown = keystatemap[0];
			for (i = 1; i < 32; i++)
				keydown |= keystatemap[i];
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;
			/* release from tile mode if needed */
			if (!c->isfloating
			&& MAX(abs(ev.xmotion.x - x), abs(ev.xmotion.y - y)) > snap)
				togglefloating(NULL);

			/* calculate window movement */
			if (type == DragMove) {
				nc.x = oc.x + (ev.xmotion.x - x);
				nc.y = oc.y + (ev.xmotion.y - y);
				for (m1 = 0; m1 < LENGTH(mons)-1
				&& !INMON(nc.x + snap, nc.y + snap, mons[m1]); m1++);
				for (m2 = 0; m2 < LENGTH(mons)-1
				&& !INMON(nc.x + nc.w - snap, nc.y + nc.h - snap, mons[m2]); m2++);
				/* snap to edges */
				if (abs(mons[m1].mx - nc.x) < snap)
					nc.x = mons[m1].mx;
				else if (abs((mons[m2].mx + mons[m2].mw) - (nc.x + WIDTH(c))) < snap)
					nc.x = mons[m2].mx + mons[m2].mw - WIDTH(c);
				if (abs(WINY(mons[m1]) - nc.y) < snap)
					nc.y = WINY(mons[m1]);
				else if (abs((WINY(mons[m2]) + WINH(mons[m2]))
				             - (nc.y + HEIGHT(c))) < snap)
					nc.y = WINY(mons[m2]) + WINH(mons[m2]) - HEIGHT(c);
			}
			else if (type == DragSize) {
				nc.w = MAX(oc.w + (ev.xmotion.x - x), 1);
				nc.h = MAX(oc.h + (ev.xmotion.y - y), 1);
				for (m2 = 0; m2 < LENGTH(mons)-1
				&& !INMON(nc.x + nc.w - snap, nc.y + nc.h - snap, mons[m2]); m2++);
				/* snap to edges */
				if (abs((mons[m2].mx + mons[m2].mw) - (c->x + nc.w + 2*c->bw)) < snap)
					nc.w = mons[m2].mx + mons[m2].mw - c->x - 2*c->bw;
				if (abs((WINY(mons[m2]) + WINH(mons[m2]))
				        - (c->y + nc.h + 2*c->bw)) < snap)
					nc.h = WINY(mons[m2]) + WINH(mons[m2]) - c->y - 2*c->bw;
			}
			if (c->isfloating)
				resize(c, nc.x, nc.y, nc.w, nc.h);
			break;
		}
	} while (ev.type != ButtonRelease && keydown);
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	grabguard = 0;

	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if (sel)
		grabbuttons(sel);
}

void
grabresizecheck(Client *c) {
	int x, y, di, abort = 0;
	unsigned int mask, dui;
	Window dummy, cw;
	XEvent ev;

	if (!(c = sel) || c->isfullscreen || !c->bw || !c->isfloating)
		return;
	if (!XQueryPointer(dpy, root, &dummy, &dummy, &x, &y, &di, &di, &mask))
		return;
	if (mask & (Button1Mask|Button2Mask|Button3Mask|Button4Mask|Button5Mask))
		return;
	if (!MOVEZONE(c, x, y) && !RESIZEZONE(c, x, y))
		return;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
		return;

	do {
		XMaskEvent(dpy,
			MOUSEMASK|ExposureMask|SubstructureRedirectMask|KeyPressMask|KeyReleaseMask,
			&ev);
		switch(ev.type) {
		case KeyPress:
			XUngrabPointer(dpy, CurrentTime);
			abort = 1;
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			x = ev.xmotion.x_root;
			y = ev.xmotion.y_root;
			break;
		case ButtonPress:
			if (ev.xbutton.button == Button1) {
				if (MOVEZONE(c, x, y))
					grabresize(&(Arg){.i = DragMove});
				else if (RESIZEZONE(c, x, y))
					grabresize(&(Arg){.i = DragSize});
			}
		}
	} while (ev.type != ButtonPress && ev.type != ButtonRelease && !abort
		&& (MOVEZONE(c, x, y) || RESIZEZONE(c, x, y)));
	XUngrabPointer(dpy, CurrentTime);

	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if (!XQueryPointer(dpy, root, &dummy, &cw, &di, &di, &di, &di, &dui))
		return;
	if (cw && (c = wintoclient(cw)))
		focus(c);
}


void
grabstack(const Arg *arg)
{
	if (!stackgrabbed)
		XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync,
			CurrentTime);
	stackgrabbed = 1;
	focusstack(arg);
}

void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
keyrelease(XEvent *e)
{
	if (stackgrabbed && grabstackrelease
		== XKeycodeToKeysym(dpy, (KeyCode)e->xkey.keycode, 0)) {
		if (sel)
			pop(sel);
		XUngrabKeyboard(dpy, CurrentTime);
		stackgrabbed = 0;
	}
}

void
killclient(const Arg *arg)
{
	if (!sel)
		return;
	if (!sendevent(sel, wmatom[WMDelete])) {
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
manage(Window w, XWindowAttributes *wa)
{
	int x, y, m;
	Client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	c->zenping = 0;
	/* geometry */
	c->x = c->fx = wa->x;
	c->y = c->fy = wa->y;
	c->w = c->fw = wa->width;
	c->h = c->fh = wa->height;
	c->oldbw = wa->border_width;

	updatetitle(c);
	strcpy(c->zenname, c->name);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->tags = t->tags;
	} else {
		c->isfloating = 1;
		c->tags = tagset[seltags];
	}

	/* find current monitor */
	if (getrootptr(&x, &y)) {
		for (m = LENGTH(mons)-1; m > 0 && !INMON(x, y, mons[m]); m--);
	} else m = 0;
	/* adjust to current monitor */
	if (c->x + WIDTH(c) > mons[m].mx + mons[m].mw)
		c->x = c->fx = mons[m].mx + mons[m].mw - WIDTH(c);
	if (c->y + HEIGHT(c) > mons[m].my + mons[m].mh)
		c->y = c->fy = mons[m].my + mons[m].mh - HEIGHT(c);
	c->x = c->fx = MAX(c->x, mons[m].mx);
	/* only fix client y-offset, if the client center might cover the bar */
	c->y = c->fy =
		MAX(c->y, ((by == mons->my) && (c->x + (c->w / 2) >= mons->mx)
			&& (c->x + (c->w / 2) < mons->mx + mons->mw)) ? bh : mons->my);
	c->bw = borderpx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c); /* propagates border_width, if size doesn't change */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask
		|PropertyChangeMask|StructureNotifyMask);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	if (c->isfloating) {
		XRaiseWindow(dpy, c->win);
		raised = c;
	}
	attach(c);
	grabbuttons(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	setclientstate(c, NormalState);
	unfocus(sel, 0);
	sel = c;
	arrange();
	XMapWindow(dpy, c->win);
	focus(NULL);
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
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	if (!XGetWindowAttributes(dpy, ev->window, &wa))
		return;
	if (wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void
moveview(const Arg *arg)
{
	if (sel)
		sel->tags = shiftviews(sel->tags, arg->i);
	focusview(arg);
}

Client *
nexttiled(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

void
pop(Client *c)
{
	detach(c);
	attach(c);
	focus(c);
	arrange();
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
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbar(0);
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if (c == sel)
				if (!zenmode || (ev->time - c->zenping) > (zenmode * 1000)) {
					strcpy(c->zenname, c->name);
					drawbar(0);
				}
			c->zenping = ev->time;

		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
quit(const Arg *arg)
{
	running = 0;
}

void
rawmotion(XEvent *e)
{
	int rx, ry, bf, di;
	unsigned int dui;
	Window cw, dummy;
	Client *c;

	if (!XQueryPointer(dpy, root, &dummy, &cw, &rx, &ry, &di, &di, &dui))
		return;

	/* top bar raise when mouse hits the screen edge.
	   especially useful for apps that capture the kayboard. */
	bf = topbar ? ry <= mons->my : ry >= mons->my + mons->mh - 1;
	bf = bf && (rx >= mons->mx) && (rx <= mons->mx + mons->mw);
	if (bf & barfocus) {
		XRaiseWindow(dpy, barwin);
		if (sel)
			unfocus(sel, 1);
	}
	else if (!bf && barfocus && sel) {
		XRaiseWindow(dpy, lastraised ? *lastraised : sel->win);
		XSetWindowBorder(dpy, sel->win, scheme[SchemeSel][ColBorder].pixel);
		setfocus(sel);
	}
	barfocus = bf;

	/* watch for border edge locations for resizing */
	if (cw && cw != root && (c = wintoclient(cw)))
		grabresizecheck(c);
}
void
resize(Client *c, int x, int y, int w, int h)
{
	if (applysizehints(c, &x, &y, &w, &h))
		resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;

	c->x = wc.x = x;
	c->y = wc.y = y;
	c->w = wc.width = w;
	c->h = wc.height = h;
	if (c->isfloating && !c->isfullscreen) {
		c->fx = c->x;
		c->fy = c->y;
		c->fw = c->w;
		c->fh = c->h;
	}
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

void
restack(void)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(0);
	if (!sel)
		return;
	XLowerWindow(dpy, barwin);
	{
		wc.stack_mode = Below;
		wc.sibling = barwin;
		for (c = clients; c; c = c->next)
			if (!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
			else if (c->isfullscreen && c != sel)
				XLowerWindow(dpy, c->win);
	}
	if (!barfocus)
		XRaiseWindow(dpy, barwin);
	lastraised = &sel->win;
	XRaiseWindow(dpy, sel->win);
	raised = sel;
	if (barfocus)
		XRaiseWindow(dpy, barwin);
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void)
{
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
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
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
setfocus(Client *c)
{
	if (!c->neverfocus) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *) &(c->win), 1);
	}
	sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *c, int fullscreen)
{
	int w, h, m1, m2;

	if (fullscreen && !c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = 1;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		/* find the full screen spread across the monitors */
		for (m1 = LENGTH(mons)-1; m1 > 0 && !INMON(c->x, c->y, mons[m1]); m1--);
		for (m2 = 0; m2 < LENGTH(mons)
		&& !INMON(c->x + c->w, c->y + c->h, mons[m2]); m2++);
		if (m2 == LENGTH(mons) || mons[m2].mx + mons[m2].mw <= mons[m1].mx
		|| mons[m2].my + mons[m2].mh <= mons[m1].my)
			m2 = m1;
		/* apply fullscreen window parameters */
		w = mons[m2].mx - mons[m1].mx + mons[m2].mw;
		h = mons[m2].my - mons[m1].my + mons[m2].mh;
		resizeclient(c, mons[m1].mx, mons[m1].my, w, h);
		XRaiseWindow(dpy, c->win);
		raised = c;
	} else if (!fullscreen && c->isfullscreen){
		/* change back to original floating parameters */
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = 0;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->fx;
		c->y = c->fy;
		c->w = c->fw;
		c->h = c->fh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange();
	}
}

void
setup(void)
{
	int i, di, xre;
	unsigned char xi[XIMaskLen(XI_RawMotion)] = { 0 };
	XIEventMask evm;
	XSetWindowAttributes wa;
	Atom utf8string;

	/* clean up any zombies immediately */
	sigchld(0);

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;
	tagset[0] = tagset[1] = 1;
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResize] = drw_cur_create(drw, XC_sizing);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	/* init appearance */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 3);
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* init monitor layout */
	bh = drw->fonts->h + 2;
	if (MONNULL(mons[0])) {
		updatemonitors(NULL);
		/* select xrandr events (if monitor layout isn't hard configured) */
		if (XRRQueryExtension(dpy, &xre, &di)) {
			handler[xre + RRNotify_OutputChange] = updatemonitors,
			XRRSelectInput(dpy, root, RROutputChangeNotifyMask);
		}
	}
	by = topbar ? mons->my : mons->my + WINH(mons[0]);
	/* init bars */
	updatebars();
	updatestatus();
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|EnterWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
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


void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

unsigned int
shiftviews(unsigned int v, int i) {
	if (i < 0)
		return (v >> -i) | (v << (LENGTH(tags) + i));
	return (v << i) | (v >> (LENGTH(tags) - i));
}

void
showhide(Client *c)
{
	if (!c)
		return;
	if (ISVISIBLE(c)) {
		/* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if (c->isfloating && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h);
		showhide(c->next);
	} else {
		/* hide clients bottom up */
		showhide(c->next);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	}
}

void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("can't install SIGCHLD handler:");
	while (0 < waitpid(-1, NULL, WNOHANG));
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
}

void
tag(const Arg *arg)
{
	if (sel && arg->ui & TAGMASK) {
		if (sel->tags == (arg->ui & TAGMASK)) {
			focusstack(&(Arg){.i = -1});
			return;
		}
		sel->tags = arg->ui & TAGMASK;
		focus(NULL);
		arrange();
	}
}

void
tile(void)
{
	unsigned int i, n, h, mw, my, ty;
	Client *c;

	for (n = 0, c = nexttiled(clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;

	mw = n > 1 ? mons[0].mw * mfact : mons[0].mw;
	for (i = my = ty = 0, c = nexttiled(clients); c; c = nexttiled(c->next), i++)
		if (i < 1) {
			h = (WINH(mons[0]) - my) / (MIN(n, 1) - i);
			resize(c, mons[0].mx, WINY(mons[0]) + my, mw - (2*c->bw), h - (2*c->bw));
			if (my + HEIGHT(c) < WINH(mons[0]))
				my += HEIGHT(c);
		} else {
			h = (WINH(mons[0]) - ty) / (n - i);
			resize(c, mons[0].mx + mw, WINY(mons[0]) + ty,
				mons[0].mw - mw - (2*c->bw), h - (2*c->bw));
			if (ty + HEIGHT(c) < WINH(mons[0]))
				ty += HEIGHT(c);
		}
}

void
togglefloating(const Arg *arg)
{
	if (!sel)
		return;
	if (sel->isfullscreen) setfullscreen(sel, 0);
	sel->isfloating = !sel->isfloating || sel->isfixed;
	if (sel->isfloating) {
		resize(sel, sel->fx, sel->fy,
			sel->fw, sel->fh);
	}
	arrange();
}

void
togglefullscreen(const Arg *arg)
{
	if (sel) setfullscreen(sel, !sel->isfullscreen);
}

void
toggletag(const Arg *arg)
{
	unsigned int newtags;

	if (!sel)
		return;
	newtags = sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		sel->tags = newtags;
		focus(NULL);
		arrange();
	}
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = tagset[seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		tagset[seltags] = newtagset;
		focus(NULL);
		arrange();
	}
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, int destroyed)
{
	XWindowChanges wc;

	detach(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	focus(NULL);
	updateclientlist();
	arrange();
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	}
}

void
updatebars(void)
{
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		.event_mask = ButtonPressMask|ExposureMask
	};
	XClassHint ch = {"dwm", "dwm"};
	if (!barwin) {
		barwin = XCreateWindow(dpy, root, mons->mx, by, mons->mw, bh, 0, DefaultDepth(dpy, screen),
				CopyFromParent, DefaultVisual(dpy, screen),
				CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, barwin, cursor[CurNormal]->cursor);
		XMapRaised(dpy, barwin);
		XSetClassHint(dpy, barwin, &ch);
	}
}

void
updateclientlist()
{
	Client *c;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (c = clients; c; c = c->next)
		XChangeProperty(dpy, root, netatom[NetClientList],
			XA_WINDOW, 32, PropModeAppend,
			(unsigned char *) &(c->win), 1);
}

void
updatemonitors(XEvent *e)
{
	int i, pri = 0, n;
	Monitor m;
	XRRMonitorInfo *inf;

	inf = XRRGetMonitors(dpy, root, 1, &n);
	for (i = 0; i < n && i < LENGTH(mons); i++) {
		SETMON(mons[i], inf[i])
		if (inf[i].primary)
			pri = i;
	}
	/* push the primary monitor to the top */
	m = mons[pri];
	mons[pri] = mons[0];
	mons[0] = m;

	/* update layout */
	by = topbar ? mons->my : mons->my + WINH(mons[0]);
	if (barwin)
		XMoveResizeWindow(dpy, barwin, mons->mx, by, mons->mw, bh);
}

void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else
		c->minw = c->minh = 0;
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
}

void
updatestatus(void)
{
	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, "  FiletLignux  ");
	drawbar(1);
}

void
updatetitle(Client *c)
{
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}

void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = 1;
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

void
view(const Arg *arg)
{
	if ((arg->ui & TAGMASK) == tagset[seltags]) {
		focusstack(&(Arg){.i = +1});
		return;
	}
	seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		tagset[seltags] = arg->ui & TAGMASK;
	focus(NULL);
	arrange();
}

Client *
wintoclient(Window w)
{
	Client *c;

	for (c = clients; c; c = c->next)
		if (c->win == w)
			return c;
	return NULL;
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
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("dwm: another window manager is already running");
	return -1;
}

void
zoom(const Arg *arg)
{
	Client *c = sel;
	if (!c || c == clients) return;
	pop(c);
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION);
	else if (argc != 1)
		die("usage: dwm [-v]");
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
	checkotherwm();
	setup();
#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
