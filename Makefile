# filetwm - dynamic window manager (filet-lignux fork of dwm)
# See LICENSE file for copyright and license details.

VERSION = filetwm-8.0

INCS = -I/usr/X11R6/include -I/usr/include/freetype2

all: filetwm

.c.o:
	cc -c -std=c99 -D_POSIX_C_SOURCE=199309L -pedantic -Wall -Os ${INCS} -DVERSION=\"${VERSION}\" $<

filetwm: filetwm.o
	cc -rdynamic -o $@ $? -lX11 -lXi -lfontconfig -lXft -lXrandr -ldl

clean:
	rm -f filetwm filetwm.o

.PHONY: all clean
