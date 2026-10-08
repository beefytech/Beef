#!/bin/bash
# Runs under a headless weston by default (needs weston and a GPU render node); --display xvfb uses xvfb-run.
# Without a GPU render node (CI runners, Xvfb) Mesa renders with llvmpipe, which loads the system LLVM into the
# IDE process and can crash when that differs from the IDE's LLVM. GALLIUM_DRIVER=softpipe avoids the crash.
# LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe reproduces those software GL conditions on a machine with a GPU.
echo Starting test_ide.sh

SCRIPTPATH=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
ROOTPATH="$(dirname "$SCRIPTPATH")"
TEST_TIMEOUT=${TEST_TIMEOUT:-180s}
DISPLAY_MODE=weston
BINARIES=(BeefIDE_d BeefIDE)

set -e

while [[ $# -gt 0 ]]
do
	case $1 in
		fast)
			echo "Performing fast test (Debug only)"
			BINARIES=(BeefIDE_d)
			;;
		--display)
			DISPLAY_MODE=$2
			shift
			;;
		*)
			echo "Usage: $0 [fast] [--display xvfb|weston]" >&2
			exit 1
			;;
	esac
	shift
done

fail() {
	if [[ $1 == 124 || $1 == 137 ]]; then
		echo "Timed out after $TEST_TIMEOUT"
	fi
	echo "#### FAILED ####"
	exit "$1"
}

if [[ $DISPLAY_MODE == "xvfb" ]]; then
	if ! command -v xvfb-run >/dev/null; then
		echo "ERROR: xvfb-run not found, install xvfb or use --display weston" >&2
		exit 1
	fi
	LAUNCHER=(timeout --kill-after=10s "$TEST_TIMEOUT" xvfb-run -a)
	# SDL3 prefers Wayland, so an inherited WAYLAND_DISPLAY would bypass Xvfb and open windows on the desktop.
	unset WAYLAND_DISPLAY
	export SDL_VIDEO_DRIVER=x11
elif [[ $DISPLAY_MODE == "weston" ]]; then
	LAUNCHER=(timeout --kill-after=10s "$TEST_TIMEOUT")
	if ! command -v weston >/dev/null; then
		echo "ERROR: weston not found, install weston or use --display xvfb" >&2
		exit 1
	fi
	if [ -z "$XDG_RUNTIME_DIR" ]; then
		echo "ERROR: XDG_RUNTIME_DIR must be set to run weston" >&2
		exit 1
	fi
	RENDERER=--renderer=gl
	if ! weston --help 2>&1 | grep -q -- --renderer; then
		RENDERER=--use-gl
	fi
	WESTON_SOCKET=beef-test-ide-$$
	WESTON_LOG=${TMPDIR:-/tmp}/$WESTON_SOCKET.log
	echo "Starting weston on $WESTON_SOCKET, logging to $WESTON_LOG"
	weston --backend=headless $RENDERER --socket=$WESTON_SOCKET --width=1920 --height=1080 --idle-time=0 >"$WESTON_LOG" 2>&1 &
	WESTON_PID=$!
	trap 'kill $WESTON_PID 2>/dev/null; wait $WESTON_PID 2>/dev/null' EXIT
	trap 'exit 1' INT TERM
	for i in {1..100}
	do
		[ -S "$XDG_RUNTIME_DIR/$WESTON_SOCKET" ] && break
		if [[ $i == 100 ]] || ! kill -0 $WESTON_PID 2>/dev/null; then
			echo "ERROR: weston failed to create $XDG_RUNTIME_DIR/$WESTON_SOCKET within 10s, see $WESTON_LOG" >&2
			exit 1
		fi
		sleep 0.1
	done
	# Keep the IDE and the programs it debugs off the caller's X server.
	unset DISPLAY
	export WAYLAND_DISPLAY=$WESTON_SOCKET
	export SDL_VIDEO_DRIVER=wayland
else
	echo "Unknown display '$DISPLAY_MODE', expected xvfb or weston" >&2
	exit 1
fi

cd "$ROOTPATH/IDE/dist"

test_workspace() {
	for script in "$ROOTPATH/$1"/scripts/*.txt
	do
		for ide in "${BINARIES[@]}"
		do
			echo "Testing $1/scripts/$(basename "$script") in $ide"
			"${LAUNCHER[@]}" "./$ide" -proddir="$ROOTPATH/$1" -test="$script" || fail $?
		done
	done
}

# Workspaces from test_ide.bat that do not pass on Linux yet are left out.
for testpath in CompileFail001 Test1 SlotTest MemoryBreakTest BugW003 BugW004 BugW006 BugW007 BugW008 BugW009 IndentTest
do
	test_workspace IDE/Tests/$testpath
done

echo "SUCCESS!"
