# *filetwm* - Filet-Lignux's Window Manager

## Design and Engineering Philosophies

This project explores how far a software product can be pushed in terms of simplicity and minimalism, both inside and out, without losing powerful features. Window Managers are often a source of bloat, as all software tends to be. *filetwm* pushes a Window Manager to its leanest essence. It is a joy to use because it does what it needs to, and then gets out of the way. The opinions that drove the project are:

* **Complexity must justify itself**.
* Lightweight is better than heavyweight.
* Select your dependencies wisely: they are complexity, but not using them, or using the wrong ones, can lead to worse complexity.
* Powerful features are good, but simplicity and clarity are essential.
* Adding layers of simplicity, to avoid understanding something useful, only adds complexity, and is a trap for learning trivia instead of knowledge.
* Steep learning curves are dangerous, but don't just push a vertical wall deeper; learning is good, so make the incline gradual for as long as possible.
* Allow other tools to thrive - e.g: terminals don't need tabs or scrollback, that's what tmux is for.
* Fix where fixes belong - don't work around bugs in other applications, contribute to them, or make something better.
* Improvement via reduction is sometimes what a project desperately needs, because we do so tend to just add. (https://www.theregister.com/2021/04/09/people_complicate_things/)


## DWM fork

The heart of this project is a fork of dwm. This started as a programming exercise to aggressively simplify a codebase already highly respected for its simplicity. It ended up making some significant user experience changes, largely from the opinions stated above. I would best describe it now as dwm with a cleaner, simpler, and more approachable user interface, whilst still holding on to powerful features.

Significant changes:
* Unified tiling, fullscreen and floating modes.
* Simpler monitor support - unified stack and workspaces.
* Focus follows mouse and clicks raise.
* Mouse support for window movement, resizing, and tile layout adjustment.
* More familiar Alt+Tab behavior (restack on release).
* More familiar bar layout with autofocus.
* Dedicated launcher button.
* Support for pinning windows.
* Zen-mode for limiting bar updates.
* More easily customise with post-compile configuration plugins.
* A whole tonne less code.

## X11 vs Wayland

This is built on X11, not Wayland, for no other reason than timing. Shortly after this project was started, NVIDIA support for Wayland was announced. This project will not include Wayland support due to the inevitable complexities of concurrently supporting multiple interfaces. When the timing is right, this will fork into a new project which can move in the direction of Wayland.

## Building

In order to build dwm you need the Xlib header files.

```bash
make
```

## Dependencies
amixer, xsetroot, bash, slock, awk, systemctl (suspend), xbacklight, man, st

These dependencies can be changed with a configuration plugin.

## Running

There are a number of ways to launch a Window Manager (https://wiki.archlinux.org/index.php/Xinit), all of which apply to filetwm. A simple way is to switch to a virtual console and then launch filetwm with:

```bash
startx filetwm
```

## Configuration

What is a Window Manager without configuration options? As minimal as this project is, tweaking is essential for everyone to set things up just how they like things.

Customise filetwm by making a '.so' file plugin and installing it to one of filetwm's config locations:
* User config: `~/.config/filetwmconf.so`
* System config: `/etc/config/filetwmconf.so`

Here is an example config that changes the font size and customises the launcher command:
```c
/* filetwmconf.c: Source config file for filetwm's config plugin.
Build and install config with:
cc -shared -fPIC filetwmconf.c -o ~/.config/filetwmconf.so
*/
#include <unistd.h>
#define S(T, N, V) extern T N; N = V;
#define P(T, N, ...) extern T* N;static T _##N[]=__VA_ARGS__;N=_##N

void config(void) {
    S(char*, font, "monospane:bold:size=5");
    P(char*, launcher, { "launcher", "monospace:size=5", "#dddddd", "#111111", "#335577", NULL });
}
```
Save it as `filetwmconf.c`, then install it to the user config location using the command found in the comment.

Many other configurations can be made via this plugin system and supported options include: colors, layout, borders, keyboard commands, launcher command, monitor configuration, and top-bar actions. Please see the defaultconfig method in the `filetwm.c` file for more details.

If you change the behaviours around documented things, like keyboard shortcuts, you can update the Help action by creating a custom man page at `~/.config/filetwmconf.1`.

### Advanced config example
The following config exemplifies changes to every option:
```c
/* filetwmconf.c: Source config file for filetwm's config plugin.
Build and install config with:
cc -shared -fPIC filetwmconf.c -o ~/.config/filetwmconf.so
*/
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/Xlib.h>
#define LEN(X) (sizeof X / sizeof X[0])
#define S(T, N, V) extern T N; N = V;
#define V(T, N, L, ...) extern T* N;static T _##N[]=__VA_ARGS__;N=_##N L
#define P(T, N, ...) V(T,N,,__VA_ARGS__;)
#define A(T, N, ...) V(T,N,;extern int N##len; N##len = LEN(_##N),__VA_ARGS__;)
enum { ClkLauncher, ClkWinTitle, ClkStatus, ClkTagBar, ClkLast };
enum { DragMove, DragSize, DragTile, DragCheck, DragNone };
typedef struct { int mx, my, mw, mh; } Monitor;
typedef union {
  int i;
  unsigned int ui;
  const void *v;
} Arg;
typedef struct {
  unsigned int click;
  unsigned int button;
  void (*func)(const Arg *arg);
  const Arg arg;
} Button;
typedef struct {
  unsigned int mod;
  KeySym key;
  void (*func)(const Arg *);
  const Arg arg;
} Key;

/* callable actions */
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

void config(void) {
    /* appearance */
    S(int, borderpx, 5); /* border pixel width of windows */
    S(int, snap, 32); /* edge snap pixel distance */
    S(int, topbar, 0); /* 0 means bottom bar */
    S(int, zenmode, 0); /* ignores showing rapid client name changes (seconds) */
    S(char*, lsymbol, "Start>"); /* launcher symbol */
    S(char*, font, "size=10");
    P(char*, colors, { "#ddffdd", "#335533", "#338877", "#558855", "#dd6622" }); /* colors (must be five colors: fg, bg, highlight, border, sel-border) */
    A(char*, tags, { "[ ]", "[ ]", "[ ]", "[ ]"}); /* virtual workspaces (must be 32 or less, *usually*) */
    A(Monitor, mons, {{0}, {0}, {0}});
    P(float, mfact, {0.5, 0.75, 0.5}); /* factor of main area size [0.05..0.95] (for each monitor) */
    P(int, nmain, {4, 1, 4}); /* number of clients in main area (for each monitor) */

  /* commands */
    P(char*, launcher, { "dmenu_run", "-fn", "size=10", "-b", "-nf", "#ddffdd", "-sf", "#ddffdd", "-nb", "#335533", "-sb", "#338877", NULL });
    P(char*, terminal, { "xterm", NULL });
    P(char*, upvol, { "amixer", "-q", "set", "Master", "10%+", NULL });
    P(char*, downvol, { "amixer", "-q", "set", "Master", "10%-", NULL });
    P(char*, mutevol, { "amixer", "-q", "set", "Master", "toggle", NULL });
    P(char*, suspend, { "bash", "-c", "killall slock; slock", NULL });
    P(char*, dimup, { "xbacklight", "-inc", "5", NULL });
    P(char*, dimdown, { "xbacklight", "-dec", "5", NULL });
    P(char*, help, { "xterm", "-e", "bash", "-c", "man filetwm || man -l ~/.config/filetwmconf.1", NULL });

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
    {           ControlMask, XK_Tab, spawn, {.v = &launcher } },
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
    TK(1) TK(2) TK(3) TK(4)
  });

  /* bar actions */
  A(Button, buttons, {
    /* click,      button, function / argument */
    { ClkLauncher, Button1, spawn, {.v = &launcher } },
    { ClkWinTitle, Button1, focusstack, {.i = +1 } },
    { ClkWinTitle, Button3, focusstack, {.i = -1 } },
    { ClkStatus,   Button1, spawn, {.v = &help } },
    { ClkStatus,   Button2, spawn, {.v = &help } },
    { ClkStatus,   Button3, spawn, {.v = &help } },
    { ClkTagBar,   Button1, view, {0} },
    { ClkTagBar,   Button3, tag, {0} },
  });
}
```

### Status bar text
To configure the status text on the top-bar, set the name of the Root Window with a tool like `xsetroot`. There are many examples configured for other Window Managers that respect a similar interface. Check out `filetstatus` from the https://github.com/fuglaro/filet-lignux project for a solution engineered under the same philosophies as this project.

# Thanks to, grateful forks, and contributions

We stand on the shoulders of giants. They own this, far more than I do.

* https://archlinux.org/
* https://github.com
* https://github.com/torvalds/linux
* https://www.x.org/wiki/XorgFoundation
* https://suckless.org/
* https://www.texturex.com/fractal-textures/fractal-design-picture-wallpaper-stock-art-image-definition-free-neuron-chaos-fractal-fracture-broken-synapse-texture/
* https://keithp.com/blogs/Cursor_tracking/
