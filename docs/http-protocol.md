# HTTP protocol handling

This document describes how `webserv` speaks HTTP: how requests are parsed,
how responses are built, and which status codes the server produces.

## Request parsing

A client connection may send the request in any number of TCP segments. The
socket is non-blocking, so the server asks `poll()` to tell it when bytes are
available and then feeds whatever `recv` returns into the `Request` state
machine (`Request::feed`).

### Phases

`Request` has a small state machine:

| State | Meaning |
|---|---|
| `HEADERS` | still waiting for the end of the header block (`\r\n\r\n`) |
| `LENGTH` | reading a fixed-size body (`Content-Length`) |
| `CHUNK_SIZE` | reading the next chunk size line |
| `CHUNK_DATA` | reading the current chunk payload |
| `TRAILERS` | reading trailers after the last (`0`) chunk |
| `DONE` | nothing left to do |

The parser never reads the socket itself; it just consumes an internal
buffer, so it naturally supports partial reads. On every `feed` it tries to
make progress and returns one of:

| Status | Meaning | Resulting HTTP answer |
|---|---|---|
| `INCOMPLETE` | needs more bytes | keep polling |
| `COMPLETE` | request fully parsed | route it (`RequestHandler`) |
| `BAD_REQUEST` | malformed syntax | 400 Bad Request |
| `TOO_LARGE` | body exceeds the limit | 413 Payload Too Large |
| `BAD_VERSION` | unknown HTTP version | 505 HTTP Version Not Supported |

### Header phase

- Header block size is capped at 64 KiB (`kMaxHeaderBytes`); beyond that the
  request is rejected with 400.
- The first line must be `METHOD SP URI SP HTTP/x.y`. Methods and targets are
  read as-is; the version must be `HTTP/1.0` or `HTTP/1.1` (anything else is
  505). The URI must start with `/`.
- The URI is percent-decoded (so `%20`, `%2F`, `%2e%2e` are handled) and
  normalized: `.` and duplicate slashes are removed, `..` is resolved
  lexically, and any `..` that would escape the root makes the request
  invalid (400). This is the path-traversal protection: `/..%2f..%2fetc/passwd`
  becomes `/../../etc/passwd`, which normalization rejects.
- Header names are lowercased and stored in a map, making lookups
  case-insensitive. Duplicate `Content-Length` headers, non-numeric
  content lengths, headers without a colon, and (for HTTP/1.1) a missing
  `Host` header are all rejected with 400, matching common server behaviour.
- If `Transfer-Encoding: chunked` is present it takes precedence over
  `Content-Length` (as RFC 7230 prescribes, since the length of a chunked
  entity is unknown until decoded).

### Body phase

**Content-Length:** the parser waits until `Content-Length` bytes have
arrived, then copies exactly that many bytes into `body` and finishes. Any
extra bytes (pipelining) are ignored — the server closes the connection
after the response anyway.

**Chunked:** chunk size lines are parsed as hexadecimal; `;extensions` after
the size are ignored. Each chunk payload is appended to `body`, the trailing
CRLF is consumed, and the parser loops. A `0` size line moves to the trailer
phase: trailer lines are consumed until the empty line that ends the message.
The un-chunked body is what the CGI receives on stdin (see `docs/cgi.md`).

The max body size is enforced *as soon as known*: a `Content-Length` larger
than the limit is rejected before a single body byte is read; for chunked
bodies the check is `current body + next chunk`, so a huge chunk that would
overflow the limit is rejected without buffering it.

## Responses

`Response` is a plain data holder (status line + headers + body) with factory
functions. `Response::serialize` produces the wire format for a given HTTP
version (the request version, so HTTP/1.0 clients get HTTP/1.0 answers).

Every response carries:

```
HTTP/1.1 200 OK
Server: webserv
Date: Mon, 17 Aug 2026 18:42:00 GMT
Connection: close
Content-Type: text/html; charset=utf-8
Content-Length: 123
```

`Content-Length` is always present and always computed from the real body
(except for 204/redirect responses where it is explicitly 0), so clients
never have to guess where the body ends. `Connection: close` announces that
the server closes the connection afterwards — allowed by HTTP/1.1 and what
keeps the state machine simple (see `docs/architecture.md`).

### Status codes the server can produce

| Code | When |
|---|---|
| 200 OK | static file, directory listing, CGI output |
| 201 Created | successful upload |
| 204 No Content | successful DELETE |
| 301/302/303 | `redirect` directives and CGI `Location:` headers |
| 400 Bad Request | malformed request line/headers, bad chunk framing |
| 403 Forbidden | directory without listing/index, POST outside upload locations |
| 404 Not Found | missing file, missing CGI script |
| 405 Method Not Allowed | method rejected by `methods` (with `Allow`) |
| 413 Payload Too Large | body over `client_max_body_size` |
| 500 Internal Server Error | internal failures (never crashes the server) |
| 501 Not Implemented | unknown method |
| 502 Bad Gateway | CGI produced no output |
| 504 Gateway Timeout | CGI killed after 30 s |
| 505 HTTP Version Not Supported | HTTP/2.x, HTTP/0.9, ... |

### Error pages

`Response::fromError` looks up the server block's `error_page` map for the
status code. If a page is configured and the file exists, it is served with
the error status. Otherwise a small built-in HTML page is generated. This is
what the subject calls "default error pages if none are provided".

### CGI output to HTTP conversion

CGI programs print headers first (possibly a `Status:` line), then the body.
`Response::fromCgi` splits the output at the first blank line, translates
the CGI headers into response headers (skipping `Status:` and a CGI-provided
`Content-Length`, which the server recomputes), and converts a `Status:`
line into the response status. A CGI output with a `Location:` header but no
`Status:` becomes a 302 redirect. If the CGI printed no header block at all,
the whole output is treated as a 200 text/html body.

## Cookies and sessions (bonus)

`Request::getCookie` parses the `Cookie` header (`name=value; name2=value2`)
case-sensitively on the name. The session demo in `/session` generates a
random id when the client has no `session_id` cookie, stores a visit counter
per id in the server process, and answers with a `Set-Cookie:
session_id=...; Path=/` header. Reloading the page reuses the session. CGI
programs can set cookies too: any header the CGI emits (including
`Set-Cookie`) is forwarded verbatim by `Response::fromCgi`.