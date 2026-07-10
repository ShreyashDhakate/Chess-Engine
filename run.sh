#!/usr/bin/env sh
# Starts gambitd and serves the board at http://127.0.0.1:$PORT
#
#   ./run.sh                 port 8080, 500ms per bot move
#   PORT=9000 ./run.sh       different port
#   MOVETIME=2000 ./run.sh   stronger bot (searches deeper)
#
# Ctrl+C stops it. On Linux this runs the binary directly; everywhere else it
# compiles and runs inside a throwaway Linux container, because the event loop
# uses epoll and epoll is a Linux syscall.
set -e

PORT=${PORT:-8080}
MOVETIME=${MOVETIME:-500}

if [ "$(uname -s)" = "Linux" ]; then
    make server
    echo "http://127.0.0.1:$PORT"
    exec ./gambitd --port "$PORT" --movetime "$MOVETIME"
fi

# Git Bash rewrites a bare "/src" into a Windows path before docker sees it,
# and its $PWD is "/c/Users/..." which docker will not mount. `pwd -W` prints
# the "C:/Users/..." form that docker wants.
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        HOSTDIR="$(pwd -W)"
        MSYS_NO_PATHCONV=1
        export MSYS_NO_PATHCONV
        ;;
    *) HOSTDIR="$(pwd)" ;;
esac

docker rm -f gambitd >/dev/null 2>&1 || true

echo "starting gambitd in a container -> http://127.0.0.1:$PORT"
echo "(first run pulls the gcc image and compiles; later runs are instant)"
echo

exec docker run --rm --name gambitd \
    -v "$HOSTDIR:/src" -w /src -p "$PORT:8080" \
    gcc:13 bash -c "make server && ./gambitd --bind 0.0.0.0 --port 8080 --movetime $MOVETIME"
