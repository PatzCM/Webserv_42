# Code walkthrough — every file, every function

This document explains the implementation line by line, file by file. Read it
in order: `main` → `utils` → `config` → `http` → `cgi` → `server`.

---

## `srcs/main.cpp`

The entry point. It validates the arguments, loads the configuration, creates
the server, runs the loop, and turns every expected failure into a clean
error message with exit code 1 — the process can never die silently.

- Lines 9-22 — one big `try`. The three catches (in order of specificity)
  print `configuration error:`, `server error:` or `fatal error:` to stderr
  and return 1. Catching `std::exception` last guarantees that even an
  unexpected exception produces a message instead of a crash.
- Line 12 — more than one argument is a usage error.
- Lines 14-15 — with no argument, `defaultConfigPath()` (lines 6-10) returns
  `configs/default.conf` if it exists in the working directory, otherwise
  `default.conf` (both relative paths). This satisfies "configuration file,
  provided as an argument ... or available in a default path".
- Line 16 — `Config(configPath)` parses the file and throws `ConfigException`
  on any syntax error.
- Line 17 — `Server(config)` keeps a reference to the config.
- Line 18 — `setup()` opens and binds all listen sockets.
- Line 19 — `run()` is the poll loop; it returns after SIGINT/SIGTERM.
- Line 21 — a normal return prints a shutdown message.

---

## `srcs/utils/Utils.cpp` — the shared toolbox

Nothing here knows about HTTP or sockets; every function is small and
self-contained, which keeps the rest of the code readable.

- `trim` — strips leading/trailing spaces, tabs, `\r` and `\n` via
  `find_first_not_of`/`find_last_not_of`. Used on header values, config
  tokens, chunk size lines.
- `toLower` — lowercase copy using `std::tolower` (the C++ `<cctype>`
  version, casting through `unsigned char` to avoid UB on non-ASCII bytes).
- `startsWith` / `endsWith` — prefix/suffix tests used for version strings,
  URI prefixes and file extensions. The implementations compare substrings,
  so `startsWith("http/1.1", "HTTP/")` is false as expected (case matters).
- `split(s, sep)` — splits on a character into `std::vector<std::string>`.
  `Request` splits header lines on `'\n'`, the config splits `host:port`.
- `join(a, b)` — concatenates `a` and `b` with exactly one `/` in between
  (unless `a` already ends with one). Used for every filesystem path
  construction; prevents double slashes.
- `intToString` — `std::ostringstream` over a long. C++98 has no
  `std::to_string`, so this is the canonical way. Used for status lines,
  Content-Length, ports, listing sizes.
- `parseUnsignedLong` — strict `strtoul`; returns false if any trailing
  garbage remains. Used for `Content-Length` parsing and config ports — not
  permissive, so `"123abc"` never passes.
- `fileExists` / `isDirectory` / `isRegularFile` — thin wrappers over
  `stat()` with the `S_ISDIR`/`S_ISREG` macros. `stat` is on the subject's
  allowed function list.
- `httpDate` — `strftime` on UTC (`gmtime_r`) producing the RFC 7231 date
  format (`Mon, 17 Aug 2026 18:42:00 GMT`) for the `Date` header.
- `localDate` — local time formatting used by the directory listing.
- `percentDecode` — walks the string; `%XX` sequences (valid hex pairs only)
  are replaced by their byte, everything else passes through. This is what
  turns `%2e%2e` into `..` for the path traversal check.
- `normalizePath(raw, clean)` — the traversal defense. Splits on `/`, walks
  the segments with a stack: `.` and empty segments are dropped, `..` pops
  the stack (and returns `false` if the stack is empty, i.e. the path would
  escape the root), everything else is pushed. The result is rebuilt with
  single slashes. `/a/../b` becomes `/b`; `/../x` is rejected.
- `baseName` — the part after the last `/`. Used to sanitize uploaded file
  names (a client cannot plant paths).
- `urlEncode` / `htmlEscape` — encode names for directory-listing links and
  escape HTML in generated pages, so file names containing `<`, `&` or
  quotes cannot break out of the page.
- `absolutePath` — `getcwd()` + `join` for relative paths. Used to give the
  CGI an absolute script path (the child `chdir`s before `execve`).
- `setNonBlocking` — `fcntl(F_GETFL)` then `fcntl(F_SETFL, flags |
  O_NONBLOCK)`. Only `F_SETFL`/`O_NONBLOCK` are used, which is exactly what
  the subject allows on macOS.
- `setCloseOnExec` — `fcntl(F_SETFD, FD_CLOEXEC)` on the CGI pipe ends, so a
  stray pipe descriptor cannot leak into another CGI.

---

## `srcs/config/` — the configuration parser

### `Config.cpp`

The parser is a small tokenizer plus two recursive parsers, all in an
anonymous `namespace` (internal linkage — no symbols leak to the linker).

- `TokenStream::tokenize` — turns the raw file content into a flat vector of
  tokens: words, `{`, `}`, `;`, with `#` to end-of-line treated as comments
  and whitespace separating words. The stream exposes `peek()`/`next()` and
  `end()`.
- `readFile` — reads the whole config into a string (`ostringstream <<
  rdbuf`), throwing if it cannot open.
- `parseSize` — `strtoul` with an optional `k`/`m` suffix (case-insensitive)
  for `client_max_body_size`. Anything else is rejected.
- `takeValue` — fetch the next token or throw "missing value" (guards
  against `listen;`).
- `consumeSemicolon` — directives *may* end with `;`; this eats it
  (leniency, matching nginx's tolerance).
- `parseLocation` — loop of expect `}` / consume directive. All the making
  of one `Location`:
  - `methods` accumulates until `;` or `}` and sets `methodsExplicit`.
  - `redirect` validates the code is 3xx and stores code + target.
  - `root`, `index`, `autoindex on|off`, `upload_path`, `session on|off`.
  - `cgi_pass` requires an extension starting with `.` and stores it with
    the interpreter.
  - unknown directives throw.
- `parseListen` — splits `host:port` on the *last* colon; empty host means
  `0.0.0.0`; another colon in the port part (IPv6) is rejected; the port
  must parse as 1-65535.
- `parseServer` — expects `server {`; dispatches `listen`, `server_name`,
  `error_page` (code 100-599 + file path), `client_max_body_size`,
  `location`; unknown directives throw; missing `}` throws at EOF.
- `Config::parseFile` — tokenizes and loops over `server` blocks. After
  parsing: at least one server must exist, and no two server blocks may
  share `host:port` (throw on duplicates — a clear error rather than
  undefined first-match behaviour).
- `findByPort` — linear scan for the server block owning a port; the poll
  loop uses it to pick the right configuration for every accepted
  connection.

### `ServerBlock.cpp`

- The constructor (lines 8-15) sets defaults (`0.0.0.0:8080`, name
  `webserv`, 1 MiB max body) and creates the implicit `Location("/")` with
  `root www`, `index index.html` — so even an empty `locations` list matches
  every request against something sane.
- `matchLocation(uri)` — the routing rule. Iterates all locations, keeps
  the *longest* prefix that matches, and requires a clean segment boundary:
  after the prefix, the next URI byte must be `/` (or the URI must end
  exactly there), so `/cgi` never hijacks `/cgi-bin`. Because the implicit
  `/` always matches, the return value is never NULL.
- `errorPageFor(code)` — looks up the code in `errorPages`; the configured
  file is only trusted if it actually exists on disk (the check happens
  here); otherwise an empty string tells the response builder to use the
  built-in page.

### `Location.cpp`

Plain data plus three helpers:

- `methodAllowed(method)` — if `methods` was never written, everything is
  allowed; otherwise membership in the vector decides.
- `allowedMethods()` — joins the list for the `Allow` header (with a
  sensible default if unset).
- `cgiInterpreter(path, out)` — takes the extension after the last `.` in
  the URL (case-insensitive) and looks it up in `cgiHandlers`; fills `out`
  with the interpreter path when found.
- `stripPrefix(uri)` — returns the part of the URI after the location
  prefix (the whole URI for `/`). This implements the subject's rooting
  rule: `/kapouet/pouic` with `root www` and `location /kapouet` resolves to
  `www/pouic`.

---

## `srcs/http/` — the wire protocol

### `Request.cpp`

The parser. Its public surface is `reset(maxBody)` (a fresh request) and
`feed(data, size)` returning one of the `Status` values. All parsing state
lives in the object, so partial reads across many `recv` calls just work.

- `kMaxHeaderBytes` (line 6): 64 KiB cap on the header block.
- `feed` (lines 19-31) — appends the new bytes to `_buffer`, then runs the
  state machine: headers phase, then the body phase matching the state, and
  reports `COMPLETE` once `_state == DONE`.
- `processHeaders` (lines 33-52) — waits for `\r\n\r\n`; if the buffer
  exceeds the header cap first, `BAD_REQUEST`. On the terminator, the head
  goes to `parseHeaderLines`, the tail stays in `_buffer` and is consumed by
  the body phase immediately — one `recv` may contain the whole request.
- `parseRequestLine` (lines 54-83) — strict: exactly three space-separated
  parts after collapsing repeated spaces; the version must literally start
  with `HTTP/` and be `1.1` or `1.0` (else `BAD_REQUEST`/`BAD_VERSION`); the
  target must start with `/`. The `?` splits path and query; the path is
  percent-decoded and normalized — an invalid normalization (traversal)
  yields `BAD_REQUEST`.
- `parseHeaderLines` (lines 85-146) — splits the head into lines, strips
  trailing `\r`, parses the request line first, then each `Name: value`
  line. Malformed lines (no colon, empty key, space in key) are 400.
  `content-length` is validated strictly and must not appear twice.
  Header names are lowercased for case-insensitive lookup. The Host check
  for HTTP/1.1 follows. Then the body mode is chosen: `Transfer-Encoding:
  chunked` wins over `Content-Length` (RFC 7230), `_bodyDeclared` is set so
  the CGI stage knows to stream a body, and `_remain` remembers how many
  bytes a length-delimited body still needs.
- `processLengthBody` (lines 148-156) — copies exactly `_remain` bytes once
  they have arrived. The `> _maxBody` check here is the backstop (the eager
  check at header time catches most cases earlier).
- `processChunked` (lines 158-209) — one loop over three sub-states:
  - *CHUNK_SIZE*: find the next `\r\n`, strip `;extensions`, parse hex with
    `strtoul(..., 16)`; a non-hex size line is 400; size 0 enters the
    trailer phase; otherwise the "current body + this chunk" overflow test
    runs before any data is buffered (`TOO_LARGE`).
  - *CHUNK_DATA*: wait until size+2 bytes exist, append the payload to
    `body`, consume the trailing CRLF.
  - *TRAILERS*: an empty line ends the message (`\r\n` right at the cursor);
    otherwise skip trailer lines one by one until it appears.
  Each sub-state consumes bytes or returns, so the loop always terminates.
- `getHeader` — case-insensitive header lookup (the map keys are already
  lowercase).
- `getCookie` — splits the `Cookie` header on `;`, trims, and returns the
  value of `name`. Used by the session demo.

### `Response.cpp`

Factories that build `Response` objects; `serialize` makes the wire format.

- `reasonPhrase(code)` — the table of standard reason phrases for every code
  the server emits.
- `fromHtml` — wraps arbitrary HTML into a full page (with the stylesheet
  linked from `www/style.css`) and sets `Content-Type: text/html;
  charset=utf-8`.
- `fromError` — tries the server block's configured error page first
  (through `ServerBlock::errorPageFor`); falls back to a small generated
  page with the code, the reason and a link home. `detail` optionally
  explains the cause.
- `fromFile` — `stat` (404 if missing, 403 if a directory), `ifstream` the
  whole file into the body, set status 200, `Content-Type` from
  `MimeTypes`, and a `Last-Modified` date. Regular disk files are read
  synchronously — the subject explicitly exempts them from the poll rule.
- `fromDirectory` — `opendir`/`readdir` (allowed functions) with a
  `closedir` guard. Each entry is stat'd for size and modification time,
  its name is HTML-escaped for display and URL-encoded for the href, and
  directories get a trailing `/` in the link. The whole thing is handed to
  `fromHtml`.
- `fromRedirect` — status + `Location` header and an explicit
  `Content-Length: 0`.
- `fromNoContent` — 204 with no body.
- `fromCreated` — 201 page naming the uploaded resource, linking to
  `/files` so the file is immediately visible.
- `fromCgi` — splits the CGI output at the first blank line (`\r\n\r\n`, or
  `\n\n` for unix-style CGI output). No blank line at all: the whole output
  is treated as a 200 text/html body. Otherwise the header block is
  translated: `Status: code` sets the status, `Content-Length` lines are
  thrown away (the server computes the real one — CGI length headers lie
  about trailing data), `Location` without a status turns the response into
  a 302, and every other header — cookies included — is forwarded.
- `serialize(version)` — `HTTP/x.y CODE REASON`, then `Server`, `Date` (UTC
  now) and `Connection: close`, then the headers map, then
  `Content-Length` (computed from the body unless one was set explicitly,
  e.g. the explicit `0` for redirects), the blank line, and the body.

### `MimeTypes.cpp`

A lazy-initialized static `std::map` of ~25 extensions; `fromPath` takes the
substring after the last `.`, lowercases it, and falls back to
`application/octet-stream`. The lazy `new` avoids static-initialization
order problems in C++98 and the map lives for the whole process.

---

## `srcs/cgi/CgiProcess.cpp` — one CGI execution

The only file allowed to call `fork()`.

- Constructor — everything starts closed/invalid (`_pid = -1`); `start`
  populates the pipes.
- `buildEnv` — composes the `char**` environment (see `docs/cgi.md` for the
  table). The `HTTP_*` loop renames request headers: uppercase, `-` → `_`.
  Each string is `new char[]`'d so `execve` gets a stable C layout.
- `freeEnv` — the mirror image; called in the parent after `fork`.
- `start` — the sequence: create both pipes (fail → false), mark the fds
  `FD_CLOEXEC`; build the environment; `fork()`; the **child** duplicates
  `pipeIn[0]` onto stdin and `pipeOut[1]` onto stdout, closes its copies of
  all four ends, `chdir`s to the working directory (so relative paths in
  the script behave — subject requirement), then `execve(interpreter,
  [interpreter, script], envp)`; on failure `exit(127)`. The **parent**
  frees the env, closes the two ends the server never uses (`pipeIn[0]`
  read end, `pipeOut[1]` write end), and switches the remaining ends to
  non-blocking. After this, only poll-driven I/O touches them.
- `closeInput` / `closeOutput` — close one end, mark it closed, set the fd
  to -1 (idempotent — called from several paths).
- `killChild` — `kill(SIGKILL)` then `reap()`.
- `reap` — `waitpid(pid, &status, WNOHANG)`; when it returns the pid, the
  child is reaped and `_pid` is reset so no double-wait happens. `WNOHANG`
  guarantees the server never blocks waiting for a child.

---

## `srcs/server/` — the runtime

### `Client.cpp`

A per-connection bundle of state; it exists so `Server` can be written
without a single client field. Everything is plain data and getters.

- `init` — (re)initializes the whole struct: request reset with the server
  block's max body size, write buffer cleared, CGI pointer nulled, timers
  set. `std::map::operator[]` default-constructs the `Client` in place, so
  no copies are ever made.
- `beginCgi` — allocates the `CgiProcess`, starts it with the interpreter
  and the *absolute* script path, resets the CGI counters. False if the
  process could not be created (pipes/fork failed).
- `clearCgi` — deletes the `CgiProcess`; its destructor kills, closes and
  reaps the child if needed. Safe to call at any state.
- `cgiBodySent` / `addCgiBodySent` — byte offset of the body already
  forwarded into the CGI's stdin (resume point for `writeToCgi`).
- `cgiTimedOut` / `setCgiTimedOut` — remembers whether `checkTimeouts`
  killed this CGI, so `finishCgi` can answer 504 instead of parsing output.
- `touch` / `lastActivity` — the idle-timeout clock, updated on every recv.

### `Server.cpp` — the poll loop (405 lines, the heart)

- Global `g_stop` + `stopHandler` (lines 16-22): SIGINT/SIGTERM set a flag;
  the loop exits at the next iteration, the destructor closes everything.
- Constructor — keeps a reference to the config.
- Destructor — closes every remaining client and listen socket. (CGIs are
  killed through `Client::clearCgi` when clients die, either here or in
  `removeClient`.)
- `createListenSocket` (37-70) — `getaddrinfo` with `AI_PASSIVE` and
  `AF_INET` (the subject allows it, `hints` restore the classic
  socket/bind/listen trio), `SO_REUSEADDR` so a restart does not hit
  TIME_WAIT, backlog 128, then the fd is switched to non-blocking. Every
  failure path closes and frees what it allocated.
- `setup` (72-88) — one listen socket per server block; failure throws
  `ServerException` with `strerror`, which `main` prints. Success prints
  the ports so the evaluator sees what is running.
- `rebuildPollfds` (90-140) — see `docs/event-loop.md`. Notice the
  `_cgiFds` map is rebuilt alongside the poll array, so the two can never
  disagree.
- `acceptClients` (142-157) — loops until `accept` fails (EAGAIN), i.e.
  drains the backlog in one event. Each new client is made non-blocking and
  initialized with the port *it arrived on* — the port is what selects the
  server block (per-connection routing, no connection to config lookup on
  every request). `inet_ntoa` gives the `REMOTE_ADDR` for the CGI env.
- `handleRequest` (159-166) — the try/catch seam: any exception escaping
  the router becomes a 500. The server cannot crash on an unexpected state.
- `readFromClient` (168-203) — drain loop. Every successful `recv` touches
  the activity clock and feeds the parser:
  - `COMPLETE` → route the request,
  - `BAD_REQUEST`/`TOO_LARGE`/`BAD_VERSION` → 400/413/505,
  - `INCOMPLETE` → keep reading.
  `recv` returning 0 is a peer close → remove the client. A negative result
  just ends the drain (no errno inspection — the fd simply gets polled
  again).
- `writeToClient` (205-221) — resume-from-offset `send` loop. Short writes
  stop the loop, the offset is persisted, and the next `POLLOUT` finishes.
  Full transfer → `removeClient`. This keeps one big response from ever
  blocking the server.
- `writeToCgi` (223-245) — the same pattern toward the child's stdin:
  resume from `cgiBodySent`, persist progress on partial writes, and when
  everything is in, close the input pipe (EOF for the child) and switch to
  `CGI_READ`.
- `readFromCgi` (247-268) — drain the child's stdout; `read` returning 0 is
  the EOF that ends the CGI.
- `finishCgi` (270-291) — close the output pipe (no more data possible),
  reap the child (no zombie), then pick the response: 504 if the timeout
  killed it, 502 if it produced nothing, else parse its output. The CGI
  object is discarded and the client moves to `WRITING`.
- `removeClient` (293-300) — drop (and CGI-clean) the client, close the fd.
  Safe to call twice for one fd (existence check).
- `respond` (302-314) — serialize the response with the *request's* HTTP
  version (falling back to 1.1 for error responses to broken requests),
  store it, flip the state to `WRITING`, reset the write offset — done via
  `Client::setWriteBuffer` which zeroes `bytesSent`.
- `checkTimeouts` (316-339) — the sweeper. Two kinds of deadline: the CGI
  deadline (kills the child first — it might be stuck in an infinite loop —
  then `finishCgi`, which will answer 504) and the idle timeout. The
  iterator is carefully restarted when a client disappears, because
  removing a client invalidates iterators.
- `run` (341-405) — signal setup (`SIGPIPE` ignored so a client that
  vanished mid-write cannot kill the process), then the infinite loop
  described in `docs/event-loop.md`. Dispatch order per fd: listen sockets
  → CGI pipes (identified through `_cgiFds`; `POLLIN` on the CGI path
  always means "output readable", `POLLOUT` "input writable") → client
  sockets. Every handler re-checks that the client still exists after each
  step, because handlers can remove clients. The `POLLERR|POLLHUP|POLLNVAL`
  branch on a CGI input pipe closes the input and falls through to
  `CGI_READ` — a dead child can still have buffered output worth reading.

### `RequestHandler.cpp` — the router

- `respond` — the same version-aware serialization helper as `Server`, used
  by all handlers.
- `resolve` — `stripPrefix` + `join` with the location root: the subject's
  rooting rule.
- `handle` — the dispatch: find the block for the port (throw → 500),
  match the location (longest prefix; never NULL thanks to the implicit
  `/`), then in order: session demo → redirect → method check (405 with
  `Allow`) → GET/POST/DELETE → 501 (with `Allow`) for anything else.
- `serveGet` — CGI extension first (missing script → 404, else
  `startCgi`); then file lookup: missing → 404, directory → index file if
  present, else autoindex listing, else 403; regular file → `fromFile`.
- `servePost` — CGI first (with the body streamed); then uploads: the
  location must have `upload_path` (else 403); extract the file name and
  payload (`extractUpload`), sanitize the name (base name only, reject `.`
  and `..`), verify the upload directory exists (500 otherwise), write the
  file (500 on failure) and answer 201.
- `serveDelete` — stat: missing → 404, directory → 403, `unlink` → 204 or
  500.
- `extractUpload` — looks for a `filename="..."` in the multipart body; the
  payload is cut between the end of the part headers and the next
  boundary (`\r\n--`). Non-multipart bodies are stored whole (useful for
  `curl -d` or chunked raw uploads). A `?name=` query parameter fills in
  when there is no filename. The name always goes through `baseName` so a
  crafted `filename="../../etc/passwd"` cannot escape the upload directory.
- `startCgi` — `Client::beginCgi` with the absolute script path (critical:
  the child `chdir`s), sets the 30 s deadline, and picks the initial state:
  body present → `CGI_WRITE` (stream the body), body absent → close the
  input pipe immediately and go straight to `CGI_READ`.
- `serveSession` — the bonus session demo: a static map id → visit count,
  session id from the cookie or generated from `rand()/time()/pid`, a
  `Set-Cookie` on the response, and a page showing the id and the count.

---

## `Makefile`

- `CXX = c++`, `CXXFLAGS = -Wall -Wextra -Werror -std=c++98` — the subject's
  exact requirements.
- Sources are listed explicitly; objects mirror the `srcs/` tree under
  `objs/`. The pattern rule `objs/%.o: srcs/%.cpp` creates directories with
  `mkdir -p` on the fly.
- `$(NAME)` depends on objects and the objects depend on their sources, so
  an up-to-date build prints *Nothing to be done for 'all'* — no relinking.
- Rules: `all`, `clean` (remove objs/), `fclean`, `re`, plus a convenience
  `run` target that launches the default configuration.