"""Check static call-site decisions on input IR without weights or debug names."""
from pathlib import Path
import re
import subprocess
import sys

opt, plugin = sys.argv[1:]
fixture = Path(__file__).with_name("ir") / "parser-hot-inline.ll"
ir = subprocess.check_output([opt, "-load-pass-plugin", plugin,
                              "-passes=parser-hot-inline,verify", "-S", fixture, "-o", "-"], text=True)
groups = dict(re.findall(r"^attributes #(\d+) = \{ ([^}]*) \}", ir, re.M))
for result, expected in [("h", "alwaysinline"), ("s", ""), ("b", "noinline")]:
    call = next(line for line in ir.splitlines() if f"%{result} = call" in line)
    group = re.search(r"#(\d+)", call)
    actual = groups[group[1]] if group else ""
    assert actual == expected, (call, actual, expected)
