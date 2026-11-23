"""Split the official January 30, 2019 ITCH 5.0 capture at message boundaries."""
import hashlib
from pathlib import Path
import sys
import tempfile
import urllib.request
import zlib

cache, output = map(Path, sys.argv[1:])
url = "https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/01302019.NASDAQ_ITCH50.gz"
length = 4 * 1024 * 1024
digest = "30815549461f9b9ff6291ac108dc78e3126be1e8ab758c6a0cc99ee2a2e32304"
if cache.is_file():
    compressed = cache.read_bytes()
else:
    request = urllib.request.Request(url, headers={"Range": f"bytes=0-{length - 1}"})
    with urllib.request.urlopen(request, timeout=60) as response:
        if response.status != 206 or not response.headers.get("Content-Range", "").startswith(
            f"bytes 0-{length - 1}/"
        ):
            raise ValueError("Nasdaq did not return the requested first 4 MiB")
        compressed = response.read(length + 1)
if len(compressed) != length or hashlib.sha256(compressed).hexdigest() != digest:
    raise ValueError("incorrect Nasdaq capture prefix")
if not cache.is_file():
    with tempfile.NamedTemporaryFile(dir=cache.parent, delete=False) as temp:
        temp.write(compressed)
    Path(temp.name).replace(cache)

data = zlib.decompressobj(wbits=31).decompress(compressed)
output.mkdir(parents=True, exist_ok=True)
offset = 0
# Three contiguous thirds of the 359,802 complete messages in the prefix.
for name in ("train", "a", "b"):
    start = offset
    for _ in range(119934):
        size = int.from_bytes(data[offset:offset + 2], "big")
        offset += 2 + size
        if size == 0 or offset > len(data):
            raise ValueError("truncated ITCH message")
    (output / name).write_bytes(data[start:offset])
if offset != 10321924:
    raise ValueError("unexpected ITCH framing")
