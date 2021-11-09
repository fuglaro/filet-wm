# filetwm - dynamic window manager (filet-lignux fork of dwm)
# See LICENSE file for copyright and license details.

INCS = -I/usr/include/freetype2

all: filetwm

.c.o:
	cc -c -std=c99 -D_DEFAULT_SOURCE -pedantic -Wall -Os ${INCS} $<

filetwm: filetwm.o
	cc -rdynamic -o $@ $? -lX11 -lXi -lfontconfig -lXft -lXrandr -ldl

clean:
	rm -f filetwm filetwm.o

.PHONY: all clean
