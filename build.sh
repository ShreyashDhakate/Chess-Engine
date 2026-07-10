#!/usr/bin/env sh
# Builds and runs everything without make or cmake.
#
#   ./build.sh          engine + protocol tests (fast, any platform)
#   ./build.sh --deep   adds the 414M-node perft reference suite
#   ./build.sh --server also builds the epoll server (Linux only)
set -e

CXX=${CXX:-g++}
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra"
ENGINE="engine/board.cpp engine/movegen.cpp engine/eval.cpp engine/search.cpp"
NET="net/sha1.cpp net/ws.cpp net/http.cpp"

DEEP=""
SERVER=""
for arg in "$@"; do
    case "$arg" in
        --deep) DEEP="--deep" ;;
        --server) SERVER="yes" ;;
    esac
done

echo "building tests..."
$CXX $CXXFLAGS -o perft       engine/board.cpp engine/movegen.cpp tests/perft.cpp
$CXX $CXXFLAGS -o regression  engine/board.cpp engine/movegen.cpp tests/regression.cpp
$CXX $CXXFLAGS -o test_search $ENGINE tests/test_search.cpp
$CXX $CXXFLAGS -o test_ws     $NET tests/test_ws.cpp

echo; ./regression
echo; ./test_ws
echo; ./test_search
echo; ./perft $DEEP

if [ -n "$SERVER" ]; then
    echo; echo "building gambitd (requires Linux: epoll)..."
    $CXX $CXXFLAGS -o gambitd $ENGINE $NET net/server.cpp
    echo "built ./gambitd -- run it with: ./gambitd --port 8080"
fi
