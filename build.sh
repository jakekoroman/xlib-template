#!/bin/sh

set -xe

cc -Wall -g -o xlib-template xlib-template.c `pkg-config x11 --libs`
