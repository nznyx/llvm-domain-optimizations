"""Check the four replay drivers against tiny inputs with known results."""
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
data, expected, malformed = cases[name]
with tempfile.TemporaryDirectory() as tmp:
    file = Path(tmp)/"input"
    file.write_bytes(data)
    _, actual = subprocess.check_output([binary, "3", file], text=True).split()
    assert int(actual) == (3 * expected) % 2**64, (name, actual, expected)
    file.write_bytes(malformed)
    assert subprocess.run([binary, "1", file]).returncode == 1, name
