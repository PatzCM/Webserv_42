# CGI support

CGI (Common Gateway Interface, RFC 3875) lets the server run external
programs and forward their output to the client. `webserv` executes a CGI
when the request URL ends with an extension mapped by `cgi_pass` in the
matched location.

## Lifecycle of a CGI request

```
                RequestHandler sees extension .py
                          |
                          v
              CgiProcess::start()
              ------------------
              pipe(stdin)  pipe(stdout)
              build environment (char**)
              fork()
              child:                          parent:
                dup2(stdin pipe -> fd 0)        close the two unused ends
                dup2(stdout pipe -> fd 1)       fcntl O_NONBLOCK on both
                close all 4 pipe ends           remaining fds
                chdir(location root)            state = CGI_WRITE / CGI_READ
                execve(interpreter, [interp, script], envp)
                on failure: exit(127)
                          |
          +---------------+----------------+
          v                                v
   request has a body?            no body
          | yes                            |
          v                                v
     CGI_WRITE                       CGI_READ
     poll(input pipe POLLOUT)       poll(output pipe POLLIN)
     write body chunk by chunk            |
     close input pipe (EOF!)              v
          |                        read output until EOF
          v                                |
     CGI_READ <---------------------------+
          |
          v
     finishCgi: parse output -> Response -> WRITING -> client
```

## The environment

`CgiProcess::buildEnv` builds the `char**` environment passed to `execve`.
The important variables (RFC 3875 + friends):

| Variable | Value |
|---|---|
| `REQUEST_METHOD` | the request method |
| `QUERY_STRING` | everything after `?` in the URL, as sent |
| `CONTENT_LENGTH` | size of the (already un-chunked) body |
| `CONTENT_TYPE` | the request's `Content-Type` header |
| `SCRIPT_FILENAME` | absolute path of the script on disk |
| `SCRIPT_NAME` | the request path (what the client typed) |
| `PATH_INFO` / `PATH_TRANSLATED` | empty / script path (kept minimal) |
| `GATEWAY_INTERFACE` | `CGI/1.1` |
| `SERVER_PROTOCOL` | `HTTP/1.0` or `HTTP/1.1` |
| `SERVER_SOFTWARE` | `webserv` |
| `SERVER_NAME` / `SERVER_PORT` | from the server block |
| `REMOTE_ADDR` | the client IP (from `accept`) |
| `DOCUMENT_ROOT` / `PWD` | the absolute location root; the child has `chdir`ed there, so relative file access works |
| `REDIRECT_STATUS` | `200` (needed by some interpreters, notably php-cgi) |
| `HTTP_*` | every request header, uppercased with `-` → `_` (`HTTP_USER_AGENT`, `HTTP_COOKIE`, ...) |

`SCRIPT_FILENAME` matters: the child `chdir`s into the location root *before*
`execve`, so the script path handed to the interpreter must be absolute —
`RequestHandler::startCgi` calls `util::absolutePath` for exactly this
reason.

## Input: body forwarding

- **No body** (GET, or POST without `Content-Length`/chunked): the input pipe
  is closed immediately, the child reads EOF and can start.
- **Body present**: the client stays in `CGI_WRITE`; the poll loop forwards
  the body into the pipe in 8 KB writes, resuming from a stored offset on
  each `POLLOUT` event. When the last byte is written, the input end is
  closed so the child sees EOF. Chunked requests arrive at this stage already
  un-chunked by the request parser, which is exactly what the CGI expects.

## Output: reading the answer

The child writes headers then body to its stdout. The server polls the read
end, drains it into `cgiOutput`, and finishes when the pipe hits EOF
(`read == 0`). EOF is the end marker — the subject explicitly notes that a
CGI without a `Content-Length` is terminated by EOF, which this design
implements naturally. `Response::fromCgi` then parses:

```
Content-Type: text/html
Set-Cookie: a=b            <- forwarded as-is (cookie bonus)
Status: 200 OK             <- overrides the status line (optional)
                            (blank line ends the header block)
...
```

## Failure modes

| Situation | Result |
|---|---|
| `fork()` or `pipe()` fails | 500 Internal Server Error |
| script file missing | 404 before the CGI is even started |
| `execve` fails | the child exits (127) with no output → 502 Bad Gateway |
| CGI prints nothing | 502 Bad Gateway |
| CGI takes more than 30 s | `SIGKILL`, reaped, → 504 Gateway Timeout |
| client disconnects mid-CGI | CGI is killed, pipes closed, process reaped |

Zombies cannot accumulate: every kill path reaps with `waitpid(pid, 0,
WNOHANG)`, and the `CgiProcess` destructor does the same as a last resort.

## Demo scripts

- `www/cgi-bin/hello.py` — echoes method, query, body and cookies back as an
  HTML page; also demonstrates GET with a query string.
- `www/cgi-bin/env.sh` — prints the CGI environment as text/plain; proves
  that a second interpreter type works (bonus: "handle multiple CGI types").
- `www/cgi-bin/slow.py` — sleeps, so `/cgi-bin/slow.py` demonstrates the 504
  timeout.

Try them:

```bash
curl "http://127.0.0.1:8080/cgi-bin/hello.py?name=palex"
curl -X POST -d "hello" http://127.0.0.1:8080/cgi-bin/hello.py
curl http://127.0.0.1:8080/cgi-bin/env.sh
curl --max-time 40 http://127.0.0.1:8080/cgi-bin/slow.py   # 504 after 30 s
```