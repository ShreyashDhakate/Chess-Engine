// Coverage-guided fuzz harness for the frame parser (libFuzzer, needs clang).
//
//   clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
//       net/sha1.cpp net/ws.cpp tests/fuzz_ws.cpp -o fuzz_ws
//   ./fuzz_ws -max_total_time=60
//
// The parser reads attacker-controlled bytes straight off a public socket, so
// the invariants that matter are memory safety and never trusting a declared
// length. tests/test_ws.cpp also runs a fixed-seed random fuzzer, which needs
// no clang and runs in CI.

#include <cstddef>
#include <cstdint>

#include "../net/ws.h"

using namespace gambit::net;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    Frame f;
    size_t consumed = 0;
    const Parse p = parseFrame(reinterpret_cast<const char*>(data), size, consumed, f);

    if (p == Parse::Ok) {
        if (consumed > size) __builtin_trap();               // never read past the buffer
        if (f.payload.size() > MAX_PAYLOAD) __builtin_trap();  // never honour a huge declared length
    } else if (p == Parse::NeedMore) {
        if (consumed != 0) __builtin_trap();                 // NeedMore must not consume
    }
    return 0;
}
