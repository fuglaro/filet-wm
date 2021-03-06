# *filetwm* - A Lean Slice of Window Manager

![](filetwm-demo.gif)[^1]
[^1]: gif is configured with the background, and terminal color scheme from filet-lignux.

filetwm is a minimalist window manager for X. It manages windows in tiled and free floating layers, across virtual workspaces, with support for fullscreen and
pinned windows.

All windows start out floating and can be switched between the tiled layer on demand. The tiling layer is arranged into columns, and adding a window to the tiling layer will create a new column. Column widths can be adjusted by resizing the top window of each column, and moving windows will shift them to different columns or move them to a different row position. The height of each window in a column can adjusted by resizing. The final columns of the monitor, and the final windows of each column, will be shrunk, if needed, to fit the monitor area. Vertical monitors follow a similar tiling layout but are organised so that the columns are arranged horizontally. In the floating layer, windows can be resized and moved freely.

Window focus follows the mouse and clicks will raise a window. If any floating window is raised, all floating windows will sit above tiled windows. Fullscreen windows that are not raised will sit behind the tiled layer.

There is a minimal status bar which contains a display of the virtual workspaces, highlighting the current selection, and a customisable status pane.

Windows have thin borders and indicate the focus state with a highlight color.

The primary control interface is intended to be via configurable keyboard shortcuts (see the help by clicking the status pane), along with mouse control for window focus, click-to-raise, and sizing movements.

It comes with a simple launcher included within the status bar. This has a similar interface to dmenu, but is much simpler. The launcher finds commands from the PATH, shown in the order they are found from the PATH components. Favorite commands should be put at the top of the PATH, in a directory with a desired listing order. Custom launchers can also be included.

See the https://github.com/fuglaro/filet-lignux project for the full filet-lignux desktop environment.

## Configuration

What is a Window Manager without configuration options? As minimal as this project is, tweaking is essential for everyone to set things up just how they like things.

Customise filetwm by making a '.so' file plugin and installing it to one of filetwm's config locations:
* User config: `~/.config/filetwmconf.so`
* System config: `/etc/config/filetwmconf.so`

Here is an example config that changes the bar font size:
```c
/* filetwmconf.c: Source config file for filetwm's config plugin.
Build and install config with:
cc -shared -fPIC filetwmconf.c -o ~/.config/filetwmconf.so
*/
#define S(T, N, V) extern T N; N = V;

void config(void) {
    S(char*, font, "monospace:size=6");
}
```
Save it as `filetwmconf.c`, then install it to the user config location using the command found in the comment.

Many other configurations can be made via this plugin system and supported options include: colors, layout, borders, keyboard commands, launcher command, monitor configuration, and bar actions. Please see the defaultconfig method in the `filetwm.c` file, or the Advanced Config example below, for more details.

If you change the behaviours around documented things, like keyboard shortcuts, you can update the Help action by creating a custom man page at `~/.config/filetwmconf.1`.

### Background image
To custimise a background image, you must apply a background to the root window. This can be done with a startup script or after launching. E.g:
```bash
feh --bg-tile ~/.config/background.jpg
```

### Status bar text
To configure the status text on the bar, you need to set the name of the Root Window with a tool like `xsetroot`. There are many examples configured for other Window Managers that respect a similar interface. The default configuration comes with an inbuilt status bar text updater called *filetstatus* which is launched in the configured startup command. See *startup* in the config section.

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

## Building

In order to build dwm you need the Xlib header files.

```bash
make
```

## Dependencies

The default configuration expects these commands to be installed:
* sh (e.g: bash)
* man
* either alacritty/st/urxvt/xterm
* xsetroot
* xbacklight (for brightness control)
* amixer, grep, sed (for volume control)
* systemctl and either slock/i3lock (for lock&suspend)

These dependencies can be changed with a custom configuration plugin.

## Running

There are a number of ways to launch a Window Manager (https://wiki.archlinux.org/index.php/Xinit), all of which apply to filetwm. A simple way is to switch to a virtual console and then launch filetwm with:

```bash
startx ./filetwm
```

## Advanced config example
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
#define LEN(X) (sizeof X / sizeof *X)
/* varible overide */
#define S(T, N, V) extern T N; N = V;
#define V(T, N, L, ...) extern T* N;static T _##N[]=__VA_ARGS__;N=_##N L
/* known length or null terminated array override */
#define P(T, N, ...) V(T,N,,__VA_ARGS__;)
/* variable length array override */
#define A(T, N, ...) V(T,N,;extern int N##len; N##len = LEN(_##N),__VA_ARGS__;)
#define RV(T, N, L, ...) do {static T _##N[] = __VA_ARGS__; N = _##N; L} while(0)
/* known length or null terminated new array declaration */
#define RP(T, N, ...) RV(T,N,,__VA_ARGS__;)
enum { ClkStatus, ClkTagBar, ClkLast };
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
void launcher(const Arg *arg);
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

char **launch;

void config(void) {
    /* appearance */
    S(int, borderpx, 5); /* border pixel width of windows */
    S(int, snap, 32); /* edge snap pixel distance */
    S(int, bar, 0); /* 0 means bottom bar */
    S(char*, font, "size=10");
    P(char*, colors, { "#ddffdd", "#335533", "#338877", "#558855", "#dd6622" }); /* colors (must be five colors: fg, bg, highlight, border, sel-border) */
    A(char*, tags, { "[ ]", "[ ]", "[ ]", "[ ]"}); /* virtual workspaces (must be 32 or less) */
    A(Monitor, mons, {{0}, {0}, {0}});

  /* commands */
    /* new declaration doesn't override an existing value but is injected via inclusion when overriding keys */
    RP(char*, launch, { "dmenu_run", "-fn", "size=10", "-b", "-nf", "#ddffdd", "-sf", "#ddffdd", "-nb", "#335533", "-sb", "#338877", NULL });
    P(char*, terminal, { "xterm", NULL });
    P(char*, upvol, { "amixer", "-q", "set", "Master", "10%+", NULL });
    P(char*, downvol, { "amixer", "-q", "set", "Master", "10%-", NULL });
    P(char*, mutevol, { "amixer", "-q", "set", "Master", "toggle", NULL });
    P(char*, suspend, { "bash", "-c", "killall slock; slock", NULL });
    P(char*, dimup, { "xbacklight", "-inc", "5", NULL });
    P(char*, dimdown, { "xbacklight", "-dec", "5", NULL });
    P(char*, help, { "xterm", "-e", "bash", "-c", "man filetwm || man -l ~/.config/filetwmconf.1", NULL });
    P(char*, poweroff, {"bash", "-c", "sudo poweroff", NULL });
    /* The startup command is run when filetwm opens.
       Please check the defaults in the code when overiding
       this as this is used to launch things like the default
       status bar text updater. This example clears that behaviour
       in case you want to manage your own status bar updater. */
    P(char*, startup, { "true", NULL };

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
    {               WinMask, XK_Tab, spawn, {.v = &launch } },
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
    {            0, XF86XK_PowerOff, spawn, {.v = &poweroff } },
    {     0, XF86XK_MonBrightnessUp, spawn, {.v = &dimup } },
    {   0, XF86XK_MonBrightnessDown, spawn, {.v = &dimdown } },
    TK(1) TK(2) TK(3) TK(4)
  });

  /* bar actions */
  A(Button, buttons, {
    /* click,      button, function / argument */
    { ClkStatus,   Button1, spawn, {.v = &launch } },
    { ClkStatus,   Button2, spawn, {.v = &terminal } },
    { ClkStatus,   Button3, spawn, {.v = &help } },
    { ClkTagBar,   Button1, view, {0} },
    { ClkTagBar,   Button3, tag, {0} },
  });
}
```

## DWM fork

The heart of this project is a fork of dwm. This started as a programming exercise to aggressively simplify a codebase already highly respected for its simplicity. It ended up making some significant user experience changes, largely from the opinions stated above. I would best describe it now as dwm with a cleaner, simpler, and more approachable user interface, whilst still holding on to powerful features.

Significant changes:
* Configurable after compilation (needed for distro packaging).
* Unified tiling, fullscreen and floating modes.
* Simpler monitor support - unified stack and workspaces.
* Focus follows mouse and clicks raise.
* Mouse support for window movement, resizing, and tile layout adjustment.
* Support for more familiar Alt+Tab behavior (restack on release).
* Bar raises with mouse move and a held key.
* Clicking on the currently selected workspace opens launcher.
* Inbuilt simple launcher included.
* Support for pinning windows.
* More easily customise with post-compile configuration plugins.
* A whole tonne less code.

## X11 vs Wayland

This is built on X11, not Wayland, for no other reason than timing. Shortly after this project was started, NVIDIA support for Wayland was announced. This project will not include Wayland support due to the inevitable complexities of concurrently supporting multiple interfaces. When the timing is right, this will fork into a new project which can move in the direction of Wayland.
It is worth considering a Rust implementation when this happens.

# Thanks to, grateful forks, and contributions

We stand on the shoulders of giants. They own this, far more than I do.

* https://suckless.org/
* https://archlinux.org/
* https://github.com
* https://github.com/torvalds/linux
* https://www.x.org/wiki/XorgFoundation
* https://www.texturex.com/fractal-textures/fractal-design-picture-wallpaper-stock-art-image-definition-free-neuron-chaos-fractal-fracture-broken-synapse-texture/
* https://keithp.com/blogs/Cursor_tracking/
