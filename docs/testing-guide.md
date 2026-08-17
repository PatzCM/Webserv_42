# Testing guide

Everything below was used to validate this implementation. The server must
be running first:

```bash
make && ./webserv configs/default.conf
```

## Quick checks with curl

```bash
B=http://127.0.0.1:8080

# static content + mime types
curl -i $B/                      # 200, index.html
curl -i $B/style.css             # Content-Type: text/css
curl -i $B/hello.txt             # Content-Type: text/plain

# errors
curl -i $B/missing-page          # 404 + custom page (errors/404.html)
curl -i $B/private/              # 403 (no index, autoindex off)
curl -i $B/upload/               # 403 (same)

# redirect
curl -i $B/old                   # 301 -> http://127.0.0.1:8080/

# directory listing
curl -i $B/files/                # 200, HTML table with notes.txt and sample.json

# CGI
curl -i "$B/cgi-bin/hello.py?name=palex"   # python CGI
curl -i $B/cgi-bin/env.sh                   # shell CGI
curl -i -X POST -d "hello body" $B/cgi-bin/hello.py   # POST with body
curl -i --max-time 40 $B/cgi-bin/slow.py    # 504 after ~30 s

# upload / delete
echo "payload" > /tmp/f.txt
curl -i -F "file=@/tmp/f.txt" $B/upload     # 201 Created
curl -i $B/upload/f.txt                     # fetch it back
curl -i -X DELETE $B/upload/f.txt           # 204 No Content
curl -i -X DELETE $B/upload/f.txt           # 404 (gone)

# limits and methods
head -c 5000000 /dev/zero | curl -X POST --data-binary @- -o /dev/null -w '%{http_code}\n' $B/upload   # 413 (limit is 4m)
curl -i -X POST $B/files/                   # 405 + Allow: GET
curl -i -X OPTIONS $B/                      # 405

# sessions (bonus)
curl -i -c /tmp/cj $B/session               # Set-Cookie: session_id=...
curl -i -b /tmp/cj $B/session               # same session, visit count 2
```

## Raw-socket checks (python3)

Things curl refuses to send, sent byte for byte:

```python
import socket
def send(raw):
    s = socket.create_connection(("127.0.0.1", 8080), timeout=5)
    s.sendall(raw)
    data = b""
    while True:
        c = s.recv(65536)
        if not c: break
        data += c
    s.close()
    print(data.split(b"\r\n",1)[0])

send(b"GARBAGE\r\n\r\n")                                  # 400
send(b"GET / HTTP/9.9\r\nHost: x\r\n\r\n")                # 505
send(b"GET / HTTP/1.1\r\n\r\n")                           # 400 (no Host)
send(b"GET /..%2f..%2fetc/passwd HTTP/1.1\r\nHost: x\r\n\r\n")  # 400 (traversal)
# chunked request with a trailer (must be 200, body echoed by the CGI):
send(b"POST /cgi-bin/hello.py HTTP/1.1\r\nHost: x\r\n"
     b"Transfer-Encoding: chunked\r\n\r\n"
     b"7\r\nchunked\r\n1\r\n!\r\n0\r\nX-T: done\r\n\r\n")
```

## Timeouts

- Half-open connection: `nc`/python connect, send nothing → closed after
  ~30 s.
- Partial request (`GET / HTTP/1.1\r\nHost: x\r\n` and stall) → closed after
  ~30 s.
- `slow.py` → 504 after ~30 s, and no zombie remains afterwards:
  `ps -eo stat,comm | grep Z` is empty.

## Stress

```bash
for i in $(seq 1 300); do curl -s -o /dev/null $B/cgi-bin/hello.py & done; wait
curl -s -o /dev/null -w '%{http_code}\n' $B/   # server still alive
```

A python threading test hammering 300 sockets against the CGI path was used
during development: 300/300 got `HTTP/1.1 200`.

## Config error handling

```bash
./webserv configs/invalid.conf   # "configuration error: server: missing '}'", exit 1
./webserv /nonexistent.conf      # "cannot open configuration file", exit 1
./webserv a b                    # usage message, exit 1
```

## Browser walkthrough (optional)

Open <http://127.0.0.1:8080/>:

1. the main page lists every feature,
2. `/files/` shows the autoindex table,
3. `/cgi-bin/hello.py?name=you` renders a CGI page,
4. `/session` sets a cookie; reloading increments the counter,
5. `/old` redirects to `/`,
6. `/private/` and `/missing-page` show the styled error pages,
7. <http://127.0.0.1:8081/> shows the second server block's site,
8. the upload form (POST to `/upload`) stores files in `www/upload/`,
   visible under `/files/` after reloading the page.

## Subject requirements checklist

| Requirement | How to verify |
|---|---|
| GET/POST/DELETE | curl above |
| static website | `/`, `/style.css`, `/hello.txt` |
| uploads | `-F file=@...` to `/upload`, file lands in `www/upload/` |
| CGI (python) | `/cgi-bin/hello.py` (GET + POST) |
| multiple CGI types (bonus) | `/cgi-bin/env.sh` |
| chunked un-chunking | raw socket test; CGI sees the plain body |
| directory listing | `/files/` |
| default file for dirs | `/` serves `index.html` |
| redirects | `/old` → 301 |
| disable listing | `/private/` → 403 |
| max body size | 5 MB body → 413 |
| custom error pages | `/missing-page` uses `errors/404.html` |
| default error pages | remove `error_page 404` from the config → built-in page |
| multiple ports/content | 8080 vs 8081 return different documents |
| no hang | slow.py 504; idle connections closed at 30 s |
| cookies/sessions (bonus) | `/session` |
| one poll(), non-blocking | `docs/event-loop.md` explains where each rule is enforced |