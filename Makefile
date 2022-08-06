SHELL = /bin/sh

all: compile

compile:
	gcc viz.c

run:
	xterm -e ./a.out
