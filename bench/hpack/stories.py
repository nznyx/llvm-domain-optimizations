"""Store each upstream story as a sequence of length-prefixed HPACK wire blocks."""
import json
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
for name, number in (("train", 21), ("a", 20), ("b", 30)):
    cases = json.loads((source / f"story_{number:02}.json").read_bytes())["cases"]
    with (output / name).open("wb") as stream:
        for case in cases:
            block = bytes.fromhex(case["wire"])
            stream.write(len(block).to_bytes(4, "big"))
            stream.write(block)
