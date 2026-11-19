"""Build selected benchmarks, then time the six configurations on the same Linux CPU."""
import argparse
import csv
import hashlib
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
modes = [("O2", "none"), ("O3", "none"), ("O3", "error"), ("O3", "x86-layout"),
         ("O3", "error,x86-layout"), ("PGO", "none")]

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("benchmarks", nargs="+",
                    choices=sorted(p.parent.name for p in (root/"bench").glob("*/CMakeLists.txt")))
parser.add_argument("--cpu", type=int, required=True)
args = parser.parse_args()
config = os.environ.get("LLVM_CONFIG", "llvm-config")
version = subprocess.check_output([config, "--version"], text=True).strip()
tools = Path(subprocess.check_output([config, "--bindir"], text=True).strip())
base = root / "build" / version

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def sample(binary, repeats, data):
    output = subprocess.check_output([binary, str(repeats), data], text=True).split()
    return float(output[0]), " ".join(output[1:])

def cpu_times(cpu):
    with open("/proc/stat") as stream:
        line = next(line for line in stream if line.startswith(f"cpu{cpu} "))
    times = list(map(int, line.split()[1:9]))
    return sum(times), times[3] + times[4]

binaries = {}
with tempfile.TemporaryDirectory() as tmp:
    section = Path(tmp) / "text"
    for bench in args.benchmarks:
        for mode, passes in modes:
            subprocess.run([root/"bench/run", bench, "a", mode, passes],
                           env={**os.environ, "REPEATS": "1"}, stdout=sys.stderr, check=True)
            binary = base/bench/f"{mode}-{passes}"/f"{bench}-bench"
            subprocess.run([tools/"llvm-objcopy", "-O", "binary", "--only-section=.text",
                            binary, section], check=True)
            binaries[bench, mode, passes] = (binary, sha(section))

os.sched_setaffinity(0, {args.cpu})
topology = Path(f"/sys/devices/system/cpu/cpu{args.cpu}/topology/thread_siblings_list")
# Linux lists siblings in cpulist notation (e.g. 2,8 or 0-1).
siblings = set()
for item in topology.read_text().strip().split(","):
    lo, _, hi = item.partition("-")
    siblings.update(range(int(lo), int(hi or lo) + 1))
siblings.discard(args.cpu)

writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(["llvm", "bench", "input", "round", "mode", "passes", "cpu", "sibling_busy_pct",
                 "repeats", "ms", "output", "input_sha256", "text_sha256"])
rng = random.Random(19)
for bench in args.benchmarks:
    for label in ("a", "b"):
        data = base/bench/"O3-none"/"inputs"/label
        digest = sha(data)
        binary, _ = binaries[bench, "O3", "none"]
        one, _ = sample(binary, 8, data)
        repeats = max(1, math.ceil(8 * 150 / one))
        _, expected = sample(binary, repeats, data)
        for round_id in range(12):
            offset = round_id % len(modes)
            if offset == 0:
                order = modes.copy()
                rng.shuffle(order)
            for mode, passes in order[offset:] + order[:offset]:
                binary, text_hash = binaries[bench, mode, passes]
                before = {cpu: cpu_times(cpu) for cpu in siblings}
                ms, output = sample(binary, repeats, data)
                busy = []
                for cpu in siblings:
                    total, idle = cpu_times(cpu)
                    ticks = total - before[cpu][0]
                    if ticks: busy.append(100 * (1 - (idle - before[cpu][1]) / ticks))
                if expected != output:
                    raise ValueError((bench, label, mode, passes, expected, output))
                writer.writerow([version, bench, label, round_id + 1, mode, passes, args.cpu,
                                 round(max(busy), 1) if busy else "", repeats, ms, output, digest, text_hash])
            sys.stdout.flush()
