*This project has been created as part of the 42 curriculum by palexand.*

## Description

**webserv** is a non-blocking HTTP/1.1 server written in C++98. It reads an
NGINX-inspired configuration file, listens on several host:port pairs at once,
serves a fully static website, handles file uploads, executes CGI programs
(python and shell), supports GET / POST / DELETE, HTTP redirects, custom error
pages, directory listing and a `client_max_body_size` limit.

The whole server runs on a single `poll()` call that monitors the listening
sockets, every client socket and every CGI pipe at the same time, for reading
and writing. Sockets and pipes are non-blocking, every request is parsed
incrementally (Content-Length and chunked bodies included), and the server
never inspects `errno` after a read or a write. `fork()` is used for CGI only.

Bonus features included: cookies and session management (`/session` demo) and
multiple CGI types (`.py` and `.sh`).

## Instructions

### Compilation

```bash
make
```

Requires a C++ compiler (g++/clang++, Linux or macOS). The Makefile provides
`all`, `clean`, `fclean`, `re` and an extra `run` target, and never relinks
unnecessary objects.

### Execution

```bash
./webserv configs/default.conf
```

Without an argument the server tries `configs/default.conf`, then
`default.conf`.

Then open <http://127.0.0.1:8080/> in a browser. The default configuration
starts two server blocks:

| Port  | Content                                                        |
|-------|----------------------------------------------------------------|
| 8080  | main site: static files, `/cgi-bin`, `/upload`, `/files`, `/old`, `/private`, `/session` |
| 8081  | second site with its own root (`www/alt/`) and error pages     |

`configs/invalid.conf` is a deliberately broken file used to demonstrate
config error handling: `./webserv configs/invalid.conf` exits with a message.

### Project layout

| Path       | Purpose                                  |
|------------|------------------------------------------|
| `incs/`    | headers (utils, config, http, server, cgi) |
| `srcs/`    | implementation                           |
| `configs/` | configuration files                      |
| `www/`     | static website, upload dir, CGI scripts  |
| `errors/`  | default error pages                      |
| `docs/`    | detailed technical documentation         |

## Resources

- [RFC 7230 — HTTP/1.1 Message Syntax and Routing](https://www.rfc-editor.org/rfc/rfc7230)
- [RFC 7231 — HTTP/1.1 Semantics and Content](https://www.rfc-editor.org/rfc/rfc7231)
- [RFC 3875 — CGI Version 1.1](https://www.rfc-editor.org/rfc/rfc3875)
- [NGINX beginner's guide](https://nginx.org/en/docs/beginners_guide.html)
- 42 Webserv subject, version 24.0

See `docs/architecture.md` for a guided tour of the implementation.
