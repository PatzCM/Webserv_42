# Configuration file reference

The configuration file is NGINX-inspired. A `server` block describes one
website (one `host:port` pair); inside it, `location` blocks describe routes.
Every directive ends with `;`, every block is delimited by `{` and `}`.
Comments start with `#` and run to the end of the line.

## Grammar

```
file        := server-block*
server-block:= "server" "{" server-directive* "}"
server-directive
            := "listen" address ";"
             | "server_name" name ";"
             | "client_max_body_size" size ";"
             | "error_page" code path ";"
             | "location" path "{" location-directive* "}"
location-directive
            := "methods" method+ ";"
             | "redirect" 3xx uri ";"
             | "root" path ";"
             | "autoindex" on|off ";"
             | "index" file ";"
             | "upload_path" path ";"
             | "session" on|off ";"
             | "cgi_pass" extension interpreter ";"
```

## Directives

### `listen address;` (server)

`address` is either `host:port` or just `port`. The default host is
`0.0.0.0`. The port must be between 1 and 65535. IPv6 is not supported and is
rejected with a clear error. Two server blocks may not listen on the same
`host:port`.

Default: `0.0.0.0:8080`.

### `server_name name;` (server)

Free-form name, passed to CGI as `SERVER_NAME`. It is informational; routing
is done by port (the virtual-host feature is explicitly out of scope in the
subject, so `server_name` is not used for matching).

### `client_max_body_size size;` (server)

Maximum accepted request body, in bytes. Suffixes `k` (KiB) and `m` (MiB) are
accepted. Bodies larger than this get a `413 Payload Too Large`.

Default: `1048576` (1 MiB).

### `error_page code path;` (server)

Maps a status code to a custom error page file (path relative to the working
directory). If the file does not exist, the built-in page is used instead.

### `location path { ... }` (server)

Opens a route for the URL prefix `path` (must start with `/`). The longest
matching prefix wins; a prefix matches only on complete path segments, so
`/cgi` never matches `/cgi-bin`. Every server block implicitly owns a `/`
location holding the defaults (`root www`, `index index.html`, all methods
allowed, autoindex off), so any request matches at least one location.

Inside a location:

#### `methods method+;`

The list of allowed HTTP methods for this route. Any other method on this
route gets `405 Method Not Allowed` with an `Allow` header. The default
allows GET, POST and DELETE everywhere.

#### `redirect 3xx uri;`

Answers every request on this location with a redirect (301/302/...) to
`uri`. Anything that is not a 3xx code is rejected at parse time.

#### `root path;`

The directory where the requested files live. **Important:** like the
subject example (`/kapouet` rooted to `/tmp/www`), the location prefix is
stripped from the URL and the remainder is appended to `root`. So with
`location /cgi-bin { root www/cgi-bin; }`, the URL `/cgi-bin/hello.py` maps
to `www/cgi-bin/hello.py`.

#### `autoindex on|off;`

If the requested path is a directory, an `on` value generates an HTML
directory listing. `off` (the default) forbids it (`403`).

#### `index file;`

The default file served when the requested path is a directory. If the index
file exists it wins over autoindex. Default: `index.html`.

#### `upload_path path;`

Marks this location as an upload endpoint: POST bodies (raw or
multipart/form-data) are written into this directory. The file name comes
from `Content-Disposition` (multipart), from a `?name=` query parameter, or
from the URL itself. Without this directive POST is refused (`403`).

#### `session on|off;`

Bonus feature: enables the in-memory session demo on this location. The
handler reads the `session_id` cookie, creates one if needed, sets a
`Set-Cookie` header and displays the visit count.

#### `cgi_pass extension interpreter;`

Maps a file extension (`.py`, `.sh`, ...) to the interpreter that executes
those files as CGI. A request whose URL ends with one of these extensions is
executed instead of being served statically. Several `cgi_pass` lines may
exist in one location. The interpreter path must be absolute.

Example:

```nginx
location /cgi-bin {
    root        www/cgi-bin;
    methods     GET POST;
    cgi_pass    .py /usr/bin/python3;
    cgi_pass    .sh /usr/bin/sh;
}
```

## Error handling

Every syntax error (unknown directive, missing `;`, unbalanced braces,
invalid port, duplicate listen) aborts the startup with a message on stderr
and a non-zero exit code:

```
$ ./webserv configs/invalid.conf
webserv: configuration error: server: missing '}'
```

## The shipped configurations

### `configs/default.conf`

One file, two server blocks:

- **127.0.0.1:8080** — `www/` with `/cgi-bin` (python + shell CGI),
  `/upload` (uploads), `/files` (autoindex), `/old` (301 redirect),
  `/private` (403), `/session` (session demo), `4m` max body size, custom
  error pages for 400/403/404/405/413/500/502/504.
- **127.0.0.1:8081** — a second site rooted at `www/alt/` with its own error
  page and `1m` body limit. Demonstrates multiple ports in one server.

### `configs/invalid.conf`

A deliberately broken file (missing closing brace) to demonstrate the parser
rejecting a malformed configuration.
