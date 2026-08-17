# webserv — architecture overview

## What this project is

`webserv` is an HTTP/1.1 server written in C++98 for the 42 subject of the
same name. It reads an NGINX-inspired configuration file, opens one listening
socket per configured `host:port`, and serves:

- static files (with MIME types),
- directory listings,
- custom and built-in error pages,
- HTTP redirects,
- file uploads (POST),
- file deletion (DELETE),
- CGI scripts (GET and POST, streamed both ways),
- cookies and sessions (bonus).

Everything runs in one process, one thread, and one `poll()` call.

## Directory layout

```
.
├── Makefile
├── README.md
├── configs/          configuration files (default.conf, invalid.conf)
├── errors/           default error pages (400..504)
├── www/              the static website
│   ├── index.html    main page
│   ├── style.css
│   ├── hello.txt
│   ├── alt/          root of the second server block (port 8081)
│   ├── cgi-bin/      hello.py, env.sh, slow.py
│   ├── files/        directory-listing demo
│   ├── private/      forbidden-directory demo (no index, no listing)
│   └── upload/       where uploaded files land
├── incs/             headers, one folder per module
│   ├── utils/        string and filesystem helpers
│   ├── config/       Config, ServerBlock, Location
│   ├── http/         Request, Response, MimeTypes
│   ├── server/       Server, Client, RequestHandler
│   └── cgi/          CgiProcess
└── srcs/             implementation, mirroring incs/
```

## Modules and responsibilities

### `utils` (Utils)
Pure helpers with no dependencies on the rest of the project: trimming,
splitting, percent-decoding, path normalization, `stat`-based existence
checks, HTTP date formatting, `fcntl` non-blocking flags. These are the small
bricks every other module uses.

### `config` (Config, ServerBlock, Location)
Parses the configuration file into plain data. `Config` owns the list of
`ServerBlock`s. Each `ServerBlock` describes one website (host, port,
server name, max body size, error page map) and owns a list of `Location`s.
Each `Location` describes what to do with one URL prefix: which methods are
allowed, the root directory, index file, autoindex flag, redirect target,
upload path, CGI extensions and session flag. The first `Location` of every
server block is always `/`, carrying the defaults, so a request always matches
something.

### `http` (Request, Response, MimeTypes)
`Request` is a state machine that consumes raw bytes and turns them into
`method`, `path`, `query`, `version`, `headers` and `body`. It can be fed
piece by piece as bytes arrive, which is exactly what a poll-based server
needs. It supports both `Content-Length` bodies and `Transfer-Encoding:
chunked` (including trailers), enforces the configured max body size and
rejects malformed requests with 400 / 505.

`Response` builds a complete HTTP response (status line, headers, body) from
semantic pieces: a file, a directory listing, an error page, a redirect, a
CGI output. `MimeTypes` maps file extensions to content types.

### `server` (Server, Client, RequestHandler)
`Server` is the heart: it owns the listening sockets, the clients, the CGI
pipe descriptors, and runs the poll loop. `Client` is the per-connection
state machine: current state (READING, WRITING, CGI_WRITE, CGI_READ), the
partial request, the pending response buffer, CGI state, activity timestamps.
`RequestHandler` is the router: it maps a request to a location and decides
what to serve.

### `cgi` (CgiProcess)
Encapsulates one CGI execution: creates the two pipes, forks, sets up the
environment, `dup2`s stdin/stdout, `execve`s the interpreter with the script
path, and exposes the non-blocking pipe ends to the poll loop. It is the only
place in the project that calls `fork()`.

## Data flow

```
            config file
                |
                v
           Config (parse)                 +-----------+
                |                         |           |
                v                         v           |
         Server::setup()         requests in/out    |
         create listen sockets        |             |
                |                     v             |
                +-------->  poll(fds) ---> dispatch by fd
                |              ^                    |
                |              |        +-----------+-----------+
                |              |        |           |           |
                |   rebuild poll set   v           v           v
                |        |      listen fd     client fd     cgi pipe fd
                |        |         |             |             |
                |        |     accept     read/write     read/write
                |        |         |             |             |
                |        |    new Client      Request       CgiProcess
                |        |         |             |
                |        |         +----> RequestHandler
                |        |                     |
                |        |              static / listing / upload /
                |        |              delete / redirect / CGI
                |        |
                +--------+
```

## The poll loop in one paragraph

Every iteration of `Server::run()` does three things:

1. **rebuild** the poll descriptor array from the current state of the world:
   listening sockets are always polled for `POLLIN`; a client is polled for
   `POLLIN` while reading its request, for `POLLOUT` while the response is
   being sent; a CGI input pipe is polled for `POLLOUT` while the request body
   is being forwarded, the output pipe for `POLLIN` while CGI output is read.
   A small map (`_cgiFds`) remembers which pipe belongs to which client so the
   event dispatch is O(1).
2. **poll** with a 500 ms timeout. The timeout is not a delay: it lets the
   loop wake up regularly to run timeouts (idle clients and stuck CGI).
3. **dispatch** every fd that became ready: accept on listening sockets, feed
   bytes to the request parser, flush the response buffer, forward the body
   into the CGI, collect the CGI output.

Because the descriptors are rebuilt from the current state before each poll,
no descriptor that is actually used is ever missing, and nothing that is no
longer needed is polled.

## The state machines

### Client states

```
        new connection
              |
              v
         READING ---------> COMPLETE request
              |                     |
              | timeout / peer     |  RequestHandler decides:
              | closed             |    * static response  -> WRITING
              |                    |    * CGI needs body   -> CGI_WRITE
              v                    |    * CGI without body -> CGI_READ
         removed                   v
        (close fd)             WRITING -----> all bytes sent
                                |  ^              |
                                |  |              |
                                |  +-- poll POLLOUT+----> removed
                                |
                                |  body fully forwarded
                                v
                            CGI_WRITE -----> input pipe closed
                                |              |
                                v              v
                            CGI_READ -----> output EOF
                                |              |
                                |              v
                                |           WRITING (CGI response)
                                |              |
                                +------> removed (after 504/502/200)
```

Every state change also changes what the next poll set contains, which is how
the whole server stays event-driven.

### Timeouts

- **Idle client**: a client that does not produce any bytes for 30 seconds
  (in READING or WRITING state) is closed. A partial request never hangs the
  server forever.
- **Stuck CGI**: a CGI that does not finish within 30 seconds is killed with
  `SIGKILL`, reaped with `waitpid(WNOHANG)`, and the client receives a
  504 Gateway Timeout.

## Design rules taken from the subject

| Subject rule | Where it is enforced |
|---|---|
| one `poll()` for all I/O | `Server::run()` only |
| monitor read and write simultaneously | clients/pipes are polled for `POLLIN`/`POLLOUT` depending on state |
| never read/write a socket or pipe without readiness | every recv/send on sockets and every read/write on pipes happens inside a poll-dispatch handler |
| no `errno` checks after read/write | none anywhere: partial writes simply stop and wait for the next event |
| non-blocking everywhere | `util::setNonBlocking` on sockets and pipe ends |
| `fork` only for CGI | only `CgiProcess::start` forks |
| C++98, `-Wall -Wextra -Werror` | Makefile |
| config file or default path | `main.cpp` falls back to `configs/default.conf` |
| request never hangs | idle + CGI timeouts above |

## Why `Connection: close` everywhere

The server closes the connection after each response. Keep-alive is legal in
HTTP/1.1 (the RFC allows the server to close at any time as long as
`Connection: close` is announced), it keeps the client state machine trivial,
and it removes an entire class of pipelining and buffer bugs. Browsers are
perfectly happy with it; each request simply opens a new TCP connection.

See also: `docs/event-loop.md` (the loop in detail), `docs/http-protocol.md`
(the wire protocol), `docs/cgi.md` (CGI specifics), `docs/code-walkthrough.md`
(every file, every function), `docs/testing-guide.md` (how to prove it works).
