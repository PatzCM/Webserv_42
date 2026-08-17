# The event loop

The whole server is driven by a single `poll()` in `Server::run()`. This
document goes through it line by line and explains the design decisions.

## Why one poll()

The subject requires:

- the server to be non-blocking at all times,
- exactly one `poll()` (or equivalent) for *all* I/O between clients and the
  server, the listening sockets included,
- that the same `poll()` monitors reading *and* writing,
- that no `read`/`write` happens on a socket or pipe without poll readiness.

The easiest way to satisfy all of these at once is to rebuild the poll
descriptor set from the server state before every single `poll()` call, and
to dispatch events based on what the states say.

## The loop

```cpp
while (!g_stop) {
    rebuildPollfds();          // 1. build the fds from current state
    int ready = poll(..., 500); // 2. wait (max 500 ms)
    if (ready > 0) {           // 3. dispatch
        for each fd with revents:
            - listen socket POLLIN          -> acceptClients
            - client READING  + POLLIN      -> readFromClient
            - client WRITING  + POLLOUT     -> writeToClient
            - cgi input  pipe POLLOUT       -> writeToCgi
            - cgi output pipe POLLIN        -> readFromCgi
            - POLLERR/POLLHUP/POLLNVAL      -> close / finish CGI
    }
    checkTimeouts();           // 4. idle clients + stuck CGI
}
```

### 1. `rebuildPollfds`

- Every listening socket is added with `events = POLLIN`.
- A client in `READING` is polled for `POLLIN`; in `WRITING` for `POLLOUT`.
  A client is never polled for both at the same time — there is no point, the
  server either waits for request bytes or flushes the response.
- In `CGI_WRITE` the CGI input pipe (the write end of the pipe that is the
  child's stdin) is polled for `POLLOUT`: the pipe is full until the child
  reads.
- In `CGI_READ` the CGI output pipe (read end of the child's stdout) is
  polled for `POLLIN`.

`_cgiFds` records `pipe fd -> client fd` so the dispatch below can find the
owner of a pipe in constant time.

### 2. The poll timeout

`poll()` gets a 500 ms timeout. This is a heartbeat, not a busy wait: when
there is no activity the loop simply wakes up twice a second to run
`checkTimeouts`, then polls again. A request can never hang the server
indefinitely because *someone* (the sweeper) always runs.

### 3. Dispatch — reading from a client

`readFromClient` drains the socket in a loop (a non-blocking `recv` until it
returns ≤ 0, which without touching errno simply means "no more data right
now"). Each chunk is handed to `Request::feed`. Depending on the parser
status:

- `COMPLETE` → `RequestHandler::handle` produces the response (state becomes
  `WRITING` with a serialized response, or the CGI states start),
- `BAD_REQUEST` → 400, `TOO_LARGE` → 413, `BAD_VERSION` → 505,
- `INCOMPLETE` → keep waiting.

`recv == 0` means the peer closed the connection; the client is removed.

### 3. Dispatch — writing to a client

`writeToClient` resumes the buffer from the recorded offset. The socket is
non-blocking, so a short write is not an error: the loop stops when `send`
returns ≤ 0, the offset is persisted, and the next `POLLOUT` event continues
where it left off. When the whole buffer is out, the connection is closed.
This is how a 1.5 MB file is streamed through 8 KB chunks without ever
blocking the server.

### 3. Dispatch — the CGI legs

`writeToCgi` forwards the request body into the CGI's stdin pipe, resuming
from the recorded offset like a client write. When the last byte is in, the
pipe is closed (`closeInput`) so the child sees EOF — this is what the
subject calls out for chunked requests: the CGI must receive the *un-chunked*
body terminated by EOF.

`readFromCgi` drains the child's stdout pipe into the client's `cgiOutput`
buffer. `read == 0` is EOF: the child is done, and `finishCgi` builds the
HTTP response (200 parse, 502 if empty, 504 if killed by the timeout).

### 3. Dispatch — error flags

`POLLERR | POLLHUP | POLLNVAL` on a client means the connection is dead:
remove it. On a CGI input pipe it means the child is not reading anymore:
close the input and switch to `CGI_READ` (the child may still be producing
output). On a CGI output pipe it means EOF even if `POLLIN` was not set:
finish the CGI. Reading never happens for an fd that poll did not mark
readable, and writing never happens for an fd poll did not mark writable —
exactly what the subject demands.

## No errno checks

The subject forbids inspecting `errno` after a read or a write. The server
never does: a negative return simply breaks the drain loop and the fd stays
in the poll set until the next event. Because every fd is non-blocking, a
temporary "not ready" condition can never stop the whole server.

## Timeouts and resilience

`checkTimeouts` runs once per loop iteration with `time(NULL)`:

- **Idle clients** (READING/WRITING, no activity for 30 s) are closed. A
  half-open connection or a stalled upload cannot occupy a slot forever.
- **CGI deadlines** (30 s) kill the child (`kill(SIGKILL)`, reaped with
  `waitpid(WNOHANG)` — the only `fork`/`kill`/`waitpid` usage in the project)
  and answer 504 Gateway Timeout.

`SIGPIPE` is ignored so a write to a socket whose peer vanished never kills
the process. `SIGINT`/`SIGTERM` set a flag that ends the loop gracefully.

## Why the server survives stress

- every operation on sockets/pipes is non-blocking and event driven,
- client bookkeeping lives in maps keyed by fd; events that arrive for a dead
  client are filtered out by existence checks before touching them,
- the request parser caps header size; the body size cap comes from the
  configuration; CGI output cannot grow unboundedly because the child is
  killed after 30 s,
- `RequestHandler::handle` is wrapped in a try/catch; an unexpected exception
  becomes a 500 response instead of a crash.

A 300-connection stress test against `/cgi-bin/hello.py` (300 simultaneous
forks + responses) completes with 300/300 200s and the server stays
responsive afterwards.