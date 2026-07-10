cmake_minimum_required(VERSION 3.16)
project(gambitd CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The engine links nothing -- no graphics, no sockets, no JSON. Keeping it
# dependency-free is what lets perft, the search tests, and the network layer
# each exercise it in isolation.
add_library(engine STATIC
  engine/board.cpp engine/movegen.cpp engine/eval.cpp engine/search.cpp)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The protocol layer is also pure: no sockets here either, so the frame codec
# and handshake are unit-testable and fuzzable on any platform.
add_library(wsproto STATIC net/sha1.cpp net/ws.cpp net/http.cpp)
target_include_directories(wsproto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(perft tests/perft.cpp)
target_link_libraries(perft PRIVATE engine)

add_executable(regression tests/regression.cpp)
target_link_libraries(regression PRIVATE engine)

add_executable(test_search tests/test_search.cpp)
target_link_libraries(test_search PRIVATE engine)

add_executable(test_ws tests/test_ws.cpp)
target_link_libraries(test_ws PRIVATE wsproto)

# epoll is Linux-only, so the server target simply does not exist elsewhere.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(gambitd net/server.cpp)
  target_link_libraries(gambitd PRIVATE engine wsproto)
endif()

enable_testing()
add_test(NAME regression  COMMAND regression)
add_test(NAME test_ws     COMMAND test_ws)
add_test(NAME test_search COMMAND test_search)
add_test(NAME perft       COMMAND perft)
