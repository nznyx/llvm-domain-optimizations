"""Check the replay drivers against tiny inputs with known results."""
import gzip
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib

name, binary = sys.argv[1:]
body = b"35=D\x0111=XYZ\x01"
header = b"8=FIX.4.2\x019=" + str(len(body)).encode() + b"\x01" + body
message = header + f"10={sum(header) % 256:03d}\x01".encode()
request = b"GET /a HTTP/1.1\r\nHost: x\r\n\r\n"
payload = b"abc" * 30000
cases = {
    "fix": (message * 2 + b"\n", 2 * (35 + 1 + 11 + 3), message.replace(b"XYZ", b"XYW")),
    "beast": (request * 2, 2 * (11 + 2 + 4 + 1), request[:-4]),
    "gzip": (gzip.compress(payload), (zlib.crc32(payload) << 32) ^ len(payload),
             gzip.compress(payload)[:-3]),
    "pugixml": (b"<book><row/><x/></book>", 10, b"<book><row></book>"),
}
# A literal inserted by the first HPACK block is referenced by the second.
literal = b"\x40\x03foo\x03bar"
hpack = len(literal).to_bytes(4, "big") + literal + b"\0\0\0\x01\xbe"
# QPACK request stream 4, static indexed field 17 (:method = GET).
qpack = (4).to_bytes(8, "big") + b"\0\0\0\x03\0\0\xd1"
timestamp = 0x010203040506
itch = b"\0\x0cS\0\x08\0\x05" + timestamp.to_bytes(6, "big") + b"O"
cases.update({
    "itch": (itch, ord("S") + 8 + 5 + timestamp, itch[:-1]),
    "hpack": (hpack, 2 * (1 + 17*3 + 31*3 + ord("f") + ord("r")), hpack[:-1]),
    "qpack": (qpack, 1 + 17*7 + 31*3 + ord(":") + ord("T"), qpack[:-1]),
    "cmake": (b"set(X 5)\n", 1931184685065656369, b"set(\0)"),
    "graphviz": (b"graph G { a -- b }", 1666928691464328521, b"graph G {\0}"),
    "jq": (b"map(.answer + 1)", 6584319999539237029, b".\0"),
    "libconfig": (b"flag = true;", 7412181703446, b"flag = \0;"),
})
for library in ("cjson", "parson", "picojson", "json11", "jansson", "boost-json"):
    cases[library] = (b'{"a":[1,{"n":2}],"b":"word"}', 3, b'{"a":[1,2}]')
data, expected, malformed = cases[name]
with tempfile.TemporaryDirectory() as tmp:
    file = Path(tmp)/"input"
    file.write_bytes(data)
    _, actual = subprocess.check_output([binary, "3", file], text=True).split()
    assert int(actual) == (3 * expected) % 2**64, (name, actual, expected)
    file.write_bytes(malformed)
    assert subprocess.run([binary, "1", file]).returncode == 1, name
