# gambitd — build the epoll server in a gcc image, run it on a slim base.
#
#   docker build -t gambitd .
#   docker run --rm -p 8080:8080 gambitd      # then open http://127.0.0.1:8080
#
# The event loop uses epoll, a Linux syscall, so the binary is Linux-only; this
# image is how it runs on macOS or Windows, and how it deploys to any host that
# takes a container (Fly.io, Render, a plain VPS).

# ---- build stage ------------------------------------------------------------
FROM gcc:13 AS build
WORKDIR /src
COPY . .
# Static libstdc++/libgcc so the runtime image needs no C++ runtime package.
RUN make server CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -static-libstdc++ -static-libgcc"

# ---- runtime stage ----------------------------------------------------------
FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /src/gambitd /app/gambitd
COPY --from=build /src/public  /app/public

# In a container the process is already isolated, so bind all interfaces and let
# the host's port mapping (or a TLS proxy) decide who can reach it.
ENV BIND=0.0.0.0
ENV PORT=8080
ENV MOVETIME=500

EXPOSE 8080
# exec form via `sh -c exec` so the ENV values expand *and* SIGTERM from
# `docker stop` reaches gambitd directly (a clean 1s shutdown, not a 10s kill).
CMD ["/bin/sh", "-c", "exec ./gambitd --bind $BIND --port $PORT --movetime $MOVETIME --public /app/public"]
