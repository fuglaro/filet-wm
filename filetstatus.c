/* See LICENSE file for copyright and license details.
 *
 * Simple updater for desktop environment status bars that respect the root
 * window name.
 *
 * Customised to run for filetwm.
 * Continuously sends updated status text to the root window name.
 *
 * "{VPN} (1*2%|2456M) [100] FiletLignux 17:22"
 *
 * {VPN} => whether or not a tun0 network connection is active.
 * (1*2%|2456M) => multiplier*maxcpu|memuse
 *   maxcpu => the percent used of the highest load CPU.
 *   multiplier => how many times higher the total cpu is over the maxcpu.
 *   memuse => memory use in Megabytes.
 * [100] => battery level.
 *
 * This file adheres to the opinion that editing C code
 * as if it is a config file, is a similar complexity level
 * to alternatives, and is a perfect learning challenge.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>

/* load file to buffer - no fault handling, only use on /proc/ files */
#define GET(F) { f=open(F, O_RDONLY);\
	r=read(f, buf, 4999*sizeof(char)); buf[r/sizeof(char)] = '\0'; close(f); }
/* search the buffer to after a string */
#define SEEK(V, buf) ((b = strstr(buf, V)) ? b += strlen(V) : NULL)
/* retrieve a 'long' from the buffer after a string */
#define L(V) (strstr(buf, V) ? strtol(strstr(buf, V)+strlen(V), &dcp, 10) : 0)
/* load the battery file to buffer with GET */
#define BAT(F) { GET("/sys/class/power_supply/BAT0/"F);\
		if (f<0) GET("/var/battery/"F); }

int
main(int argc, char *argv[])
{
	int f, r, i, j, vpn, charge;
	long bat, delta, idle, cpu, mem, maxcpu, totcpu;
	/* only b up to 64 CPUs */
	static long ctimelast[64] = {0}, cidlelast[64] = {0};
	char buf[5000] = {[4999] = '\0'}, *b, *dcp;
	time_t now;
	Display *dpy = XOpenDisplay(NULL);
	Window root = RootWindow(dpy, DefaultScreen(dpy));

	while (1) {

		/* check for vpn */
		GET("/proc/net/dev");
		vpn = r && strstr(buf, "tun0");

		/* cpu utilisation */
		maxcpu = totcpu = 0;
		GET("/proc/stat");
		/* skip the cpu totals line */
		SEEK("cpu", buf);
		/* parse each cpu line */
		for (j = 0; SEEK("cpu", b) && (b = strstr(b, " ")) && j < 64; j++) {
			idle = -cidlelast[j];
			delta = -ctimelast[j];
			for (i = 0; !strncmp(b, " ", strlen(" ")); i++) {
				if (i == 3 || i == 4)
					idle += strtol(b, &dcp, 10);
				delta += strtol(b, &b, 10);
			}
			/* update maxcpu and totcpu */
			if ((cpu = (delta-idle)*100/(delta?delta:1)) > maxcpu)
				maxcpu = cpu;
			totcpu += cpu;
			/* update static accumulators */
			ctimelast[j] += delta;
			cidlelast[j] += idle;
		}

		/* memory used */
		GET("/proc/meminfo");
		mem = L("mTotal:")-L("mFree:")-L("Buffers:")-L("Cached:")-L("claimable:");

		/* battery levels */
		BAT("capacity");
		bat = L("");
		/* battery charging */
		BAT("status");
		charge = 0==strncmp(buf, "Charging", 8);

		/* time */
		now = time(0);

		/* send output and wait interval */
		snprintf(buf, 4999*sizeof(char), "%s(%ld*%ld%%|%ldM) [%ld%s] %02d:%02d",
			vpn?"{VPN} ":"", totcpu/(maxcpu?maxcpu:1), maxcpu, mem/1024, bat,
			charge?"+":"", localtime(&now)->tm_hour, localtime(&now)->tm_min);
		XStoreName(dpy, root, (char *)buf);
		XSync(dpy, False);
		sleep(5);

	}
}
