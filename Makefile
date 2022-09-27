SHELL = /bin/sh

all: compile

compile:
	gcc viz.c -o ./rndr

run:
	xterm -e ./rndr
