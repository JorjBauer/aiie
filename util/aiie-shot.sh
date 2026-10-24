#!/bin/sh
#
# aiie-shot.sh: grab a screen from a running aiie-sdl over its debug socket.
#
#   util/aiie-shot.sh [-p port] [-h host] [-s] [-t] [-k keys] [-w seconds] [file.png]
#
#     -p port      debug socket port (default 12345, or $AIIE_DEBUG_PORT)
#     -h host      debug socket host (default 127.0.0.1, or $AIIE_DEBUG_HOST)
#     -s           the screen alone, without the bezel and drive lamps
#                  (560x384 on the 8875 panel, 280x192 on the 9341)
#     -t           also dump the text screen as ASCII, to stdout
#     -k keys      type these keystrokes first, then wait -w and shoot;
#                  the K command's escapes work ("\n" is return)
#     -w seconds   how long to wait after -k before shooting (default 2)
#     file.png     where to write the image (default aiie-shot-N.png in $PWD)
#
# The image is the panel as rendered, so it works in any mode: 40- and
# 80-column text, HGR, DHGR, lores, mixed, and the BIOS. -s gives the screen
# alone, which is what you want for documentation or for comparing against
# another emulator; without it you get the whole panel, bezel and drive lamps
# included, as the hardware's own display would show it. Use -t when you want
# the characters as text instead (greppable, and diffable in a test).
#
# The emulator writes the file itself, so the path is resolved in ITS working
# directory, not this script's: a relative path is made absolute here before
# it is sent.

set -e

HOST=${AIIE_DEBUG_HOST:-127.0.0.1}
PORT=${AIIE_DEBUG_PORT:-12345}
DUMPTEXT=no
SCREENONLY=no
KEYS=
WAIT=2
OUT=

usage() {
  sed -n '3,27p' "$0" | sed 's/^# \{0,1\}//'
  exit "$1"
}

while [ $# -gt 0 ]; do
  case "$1" in
    -p) [ $# -ge 2 ] || usage 1; PORT=$2; shift 2 ;;
    -h) [ $# -ge 2 ] || usage 1; HOST=$2; shift 2 ;;
    -s) SCREENONLY=yes; shift ;;
    -t) DUMPTEXT=yes; shift ;;
    -k) [ $# -ge 2 ] || usage 1; KEYS=$2; shift 2 ;;
    -w) [ $# -ge 2 ] || usage 1; WAIT=$2; shift 2 ;;
    --help) usage 0 ;;
    -*) echo "$0: unknown option $1" >&2; usage 1 ;;
    *)  [ -z "$OUT" ] || { echo "$0: only one output file" >&2; exit 1; }
        OUT=$1; shift ;;
  esac
done

command -v nc >/dev/null 2>&1 || { echo "$0: nc is not on PATH" >&2; exit 1; }

# One command, one connection: the socket resumes a paused machine when a
# client drops, so a shot never leaves the emulator frozen.
send() {
  printf '%s\n' "$1" | nc "$HOST" "$PORT"
}

# Anything to report before the shot? A connection refused here is the useful
# error, rather than a mystery empty PNG later.
if ! send 'y' | grep -q .; then
  echo "$0: no answer from aiie at $HOST:$PORT (is it running? was it given -p $PORT?)" >&2
  exit 1
fi

if [ -n "$KEYS" ]; then
  send "K $KEYS" >/dev/null
  sleep "$WAIT"
fi

# Default name: the first aiie-shot-N.png that is free, so repeated shots in
# one session pile up instead of overwriting each other.
if [ -z "$OUT" ]; then
  n=0
  while [ -e "aiie-shot-$n.png" ]; do n=$((n + 1)); done
  OUT="aiie-shot-$n.png"
fi

# Make the path absolute: the emulator, not this script, does the fopen.
case "$OUT" in
  /*) ;;
  *)  OUT="$PWD/$OUT" ;;
esac

if [ "$SCREENONLY" = yes ]; then
  reply=$(send "P screen $OUT")
else
  reply=$(send "P $OUT")
fi
case "$reply" in
  *"Wrote $OUT"*) echo "$OUT" ;;
  *)
    echo "$0: the emulator did not write it:" >&2
    printf '%s\n' "$reply" | tr -d '\r' | sed '/^$/d; s/^/  /' >&2
    exit 1
    ;;
esac

if [ "$DUMPTEXT" = yes ]; then
  # Strip the "debug [$XXXX]> " prompts in place rather than by the line: the
  # first one shares its line with the "mode:" report, which is worth keeping.
  send 'T' | tr -d '\r' | sed 's/debug \[\$[0-9A-Fa-f]*\]> //g' | sed '${/^$/d;}'
fi
