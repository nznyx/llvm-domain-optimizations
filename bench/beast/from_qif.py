"""Convert a QIF capture to HTTP/1.1 headers. Optionally reserve its first fifth for PGO."""
from pathlib import Path
import sys

def request(block):
    fields = [line.split(b"\t", 1) for line in block.splitlines()
              if line and not line.startswith(b"#")]
    pseudo = {k: v for k, v in fields if k.startswith(b":")}
    headers = [(k, v) for k, v in fields if not k.startswith(b":")]
    if not any(k.lower() == b"host" for k, v in headers):
        headers.insert(0, (b"Host", pseudo[b":authority"]))
    start = pseudo[b":method"] + b" " + pseudo[b":path"] + b" HTTP/1.1\r\n"
    return start + b"".join(k + b": " + v + b"\r\n" for k, v in headers) + b"\r\n"

source, target, *train = map(Path, sys.argv[1:])
requests = [request(block) for block in source.read_bytes().strip().split(b"\n\n") if block.strip()]
if train:
    n = len(requests) // 5
    training, requests = requests[:n], requests[n:]
    if not training or set(training).intersection(requests):
        raise ValueError("empty training set or duplicate requests in evaluation")
    train[0].write_bytes(b"".join(training))
target.write_bytes(b"".join(requests))
