# *filetwm* - Filet-Lignux's Window Manager

## Design and Engineering Philosophies

This project explores how far a software product can be pushed in terms of simplicity and minimalism, both inside and out, without losing powererful features. Window Managers are often a source of bloat, as all software tends to be. *filetwm* pushes a Windowm Manager to its leanest essence. It is a joy to use because it does what it needs to, and then gets out of the way. The opinions that drove the project are:

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

The heart of this project is a fork of dwm. This started as a programming exercise to aggressively simplify a codebase already highly respected for it's simplicity. It ended up making some significant user experience changes, largely from the opinions stated above. I would best describe it now as dwm with a cleanr, simpler, and more approachable user interface, whilst still holding on to powerful features.

Significant changes:
* Unified tiling, fullscreen and floating modes.
* Simpler monitor support - unified stack and workspaces.
* Focus follows mouse with click-to-raise.
* Mouse support for window movement, resizing, and tile layout adjustment.
* More familiar Alt+Tab behavior (restack on release).
* More familiar bar layout with autofocus.
* Dedicated launcher button.
* Support for pinning windows.
* Zen-mode for limiting bar updates.
* More easily customise with post-complile plugins.
* A whole chunk less code.

## X11 vs Wayland

This is built on X11, not Wayland, for no other reason than timing. Shortly after this project was started, NVIDIA support for Wayland was announced. This project will not include Wayland support due to the inevitable complexities of concurrently supporting multiple interfaces. When the timing is right, this will fork into a new project which can move in the direction of Wayland.


## Building

In order to build dwm you need the Xlib header files.

```bash
make
```

## Running

There are a number of ways to launch a Window Manager (https://wiki.archlinux.org/index.php/Xinit), all of which apply to filetwm. A simple way is to switch to a virtual console and then launch filetwm with:

```bash
startx filetwm
```

## Configuration

What is a Window Manager without configuration options? As minimal as this is, tweaking is essential for everyone to set things up just how they like them.

Customise filetwm by making a '.so' file plugin and installing it to one of filetwm's config locations:
* User config: `~/.config/filetwmconf.so`
* System config: `/etc/config/filetwmconf.so`

Here is an example config that changes the font size of the top-bar:
```c
/* Source config file for filetwm's config plugin.
Build and install config:
cc -shared -fPIC filetwmconf.c -o ~/.config/filetwmconf.so
*/

void _() {
    extern char *font; font = "monospace:bold:size=5";
}
```
Many other configurations can be made with the plugin system including for colors, layout, borders, keyboard commands, launcher, monitors, and top-bar actions. Any non-static global variable in the `filetwm.c` source file can be changed in this way. See the Configuraton Section.

### Status bar text
To configure the status text on the top-bar, set the name of the Root Window with a tool like `xsetroot`. There are many examples configured for other Window Managers that respect a similar interface. Check out `filetstatus` from the FiletLignux project for a solution engineered under the same philosophies as this project.

