# pyllhttp
Python wrapper for llhttp
======

A simple Python wrapper around [llhttp](https://github.com/nodejs/llhttp),
the HTTP parser for [Node.js](https://nodejs.org/).

```python
import llhttp
from pprint import pprint

pprint({"version": llhttp.version})

class request_parser(llhttp.Request):
    headers = {}

    url = b''
    current_header_field = None
    current_header_value = None

    def on_message_begin(self):
        print(f"MESSAGE BEGIN")

    def on_url(self, url):
        self.url += url
        self.pause()

    def on_header_field(self, field):
        if self.current_header_value is not None:
            assert self.current_header_field is not None
            self.headers[self.current_header_field] = self.current_header_value
            self.current_header_field = None
            self.current_header_value = None

        if self.current_header_field is None:
            self.current_header_field = bytes(field)
        else:
            self.current_header_field += field

    def on_header_value(self, value):
        assert self.current_header_field is not None
        if self.current_header_value is None:
            self.current_header_value = bytes(value)
        else:
            self.current_header_value += value

    def on_headers_complete(self):
        if self.current_header_value is not None:
            assert self.current_header_field is not None
            self.headers[self.current_header_field] = self.current_header_value
            self.current_header_field = None
            self.current_header_value = None

    def on_message_complete(self):
        assert self.current_header_field is None
        assert self.current_header_value is None
        print("MESSAGE COMPLETE")

parser = request_parser()

buffer = b"GET /test HTTP/1.1\r\nlol:wut\r\noh: hai\r\n\r\n"
while buffer:
    consumed = parser.execute(buffer[:2])
    buffer = buffer[consumed:]
    if parser.is_paused:
        print("UNPAUSING")
        parser.unpause()

parser.finish()
pprint({
    "method": parser.method,
    "url": parser.url,
    "version": f"{parser.major}.{parser.minor}",
    "headers": parser.headers,
})
```

This project is a toy, started to reacquaint myself with Python
[c-api](https://docs.python.org/3/c-api/) modules.  If you find it useful,
let me know.

The version number tracks the version of the incorporated llhttp.

License: [MIT](https://opensource.org/licenses/MIT)
