#!/bin/sh
ignored=$(awk '/^SigIgn:/ { print $2 }' "/proc/$$/status")
session=$(awk '{ print $6 }' "/proc/$$/stat")
pipe=default
[ $((0x$ignored >> 12 & 1)) -eq 1 ] && pipe=ignored
own=no
[ "$session" = "$$" ] && own=yes
echo "fake pacman $* sigpipe=$pipe setsid=$own"
