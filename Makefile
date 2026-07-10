CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
ENGINE   := engine/board.cpp engine/movegen.cpp engine/eval.cpp engine/search.cpp
NET      := net/sha1.cpp net/ws.cpp net/http.cpp

TESTS := perft regression test_search test_ws

.PHONY: all test deep server fuzz bench clean

all: $(TESTS)

perft: engine/board.cpp engine/movegen.cpp tests/perft.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

regression: engine/board.cpp engine/movegen.cpp tests/regression.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_search: $(ENGINE) tests/test_search.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_ws: $(NET) tests/test_ws.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# The epoll server. Linux only -- epoll has no Windows or macOS equivalent.
gambitd: $(ENGINE) $(NET) net/server.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

server: gambitd

# The suite that gates every commit: ~27M perft nodes, under 10 seconds total.
test: $(TESTS)
	./regression
	./test_ws
	./test_search
	./perft

# Full reference suite: ~414M nodes, about a minute.
deep: perft
	./perft --deep

# Coverage-guided fuzzing of the frame parser. Needs clang, not gcc.
fuzz:
	clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
		net/sha1.cpp net/ws.cpp tests/fuzz_ws.cpp -o fuzz_ws
	./fuzz_ws -max_total_time=60

bench:
	python3 tools/bench.py --clients 8 --pings 200

clean:
	rm -f $(TESTS) gambitd fuzz_ws *.exe
