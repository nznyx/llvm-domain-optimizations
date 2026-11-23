"""Draw the comparison from the paired timings in llvm23.csv."""
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import TwoSlopeNorm
import numpy as np

here = Path(__file__).resolve().parent
benches = {
    "beast": "Boost.Beast · HTTP", "fix": "hffix · FIX", "itch": "CppTrader · ITCH",
    "hpack": "nghttp2 · HPACK", "qpack": "nghttp3 · QPACK", "gzip": "zlib · gzip",
    "libcsv": "libcsv · CSV", "libyaml": "libyaml · YAML", "pugixml": "pugixml · XML",
    "cjson": "cJSON", "parson": "Parson", "picojson": "picojson", "json11": "json11",
    "jansson": "Jansson", "boost-json": "Boost.JSON", "cmake": "CMake",
    "graphviz": "Graphviz · DOT", "jq": "jq", "libconfig": "libconfig",
}
passes = {"error": "ErrorHeuristic", "x86-layout": "X86LayoutPolicy",
          "parser-hot-inline": "ParserHotInline", "transition-split": "TransitionSplit"}
variants = {"O2": "O2", "error": "Error", "x86-layout": "Layout",
            "parser-hot-inline": "HotInline", "transition-split": "Transition",
            "error,x86-layout": "Error +\nLayout",
            "error,parser-hot-inline,transition-split,x86-layout": "Все 4", "PGO": "PGO"}

rounds = defaultdict(dict)
versions = set()
binary_hashes = {}
with (here / "llvm23.csv").open() as stream:
    for row in csv.DictReader(stream):
        key = row["bench"], row["input"], int(row["round"])
        mode = row["passes"] if row["passes"] != "none" else row["mode"]
        if mode in rounds[key]:
            raise ValueError(f"duplicate measurement: {key}, {mode}")
        if binary_hashes.setdefault((row["bench"], mode), row["text_sha256"]) != row["text_sha256"]:
            raise ValueError(f"machine code changed during measurement: {key}, {mode}")
        rounds[key][mode] = row
        versions.add(row["llvm"])
assert len(versions) == 1
version, = versions
assert version.startswith("23.1.")
modes = {"O3", *variants}
round_count = 2 * len(modes)
assert set(rounds) == {(b, label, n) for b in benches for label in ("a", "b")
                       for n in range(1, round_count + 1)}

effects = defaultdict(list)
same_text = {}
for (bench, label, _), group in sorted(rounds.items()):
    assert group.keys() == modes
    base = group["O3"]
    for name in variants:
        row = group[name]
        assert all(row[k] == base[k] for k in ("input_sha256", "output", "repeats", "cpu"))
        key = bench, label, name
        effects[key].append(100 * (float(base["ms"]) / float(row["ms"]) - 1))
        identical = base["text_sha256"] == row["text_sha256"]
        assert same_text.setdefault(key, identical) == identical

rng = np.random.default_rng(23)
indices = rng.integers(0, round_count, (4000, round_count))
stats = {}
for key, values in effects.items():
    data = np.asarray(values)
    lower, upper = np.percentile(np.median(data[indices], axis=1), [2.5, 97.5])
    stats[key] = (np.median(data), lower, upper)

def separators(axis):
    for bench in ("qpack", "pugixml", "boost-json"):
        axis.axhline(list(benches).index(bench) + 0.5, color="#969ca2", lw=0.8)

fig, axis = plt.subplots(figsize=(10, 8.5), layout="constrained")
values = [[min(stats[b, label, m][0] for label in ("a", "b")) for m in variants] for b in benches]
colors = [[0 if same_text[b, "a", m] else v for m, v in zip(variants, row)]
          for b, row in zip(benches, values)]
norm = TwoSlopeNorm(vmin=-20, vcenter=0, vmax=20)
axis.imshow(colors, cmap="RdBu", norm=norm, aspect="auto")
for y, bench in enumerate(benches):
    for x, mode in enumerate(variants):
        value = values[y][x]
        label = (f"{value:+.1f}" if abs(value) >= 0.05 else "0.0") + ("*" if same_text[bench, "a", mode] else "")
        axis.text(x, y, label, ha="center", va="center", fontsize=9,
                  color="white" if abs(colors[y][x]) > 13 else "#182430")
axis.set_yticks(range(len(benches)), benches.values())
axis.set_xticks(range(len(variants)), variants.values())
axis.xaxis.tick_top()
axis.tick_params(length=0, pad=5)
separators(axis)
axis.set_title(f"Меньшее ускорение из двух входов относительно −O3, %   ·   LLVM {version}",
               weight="bold", pad=18)
axis.set_xlabel("* Секция .text совпадает с −O3. Насыщенность цвета ограничена при ±20%.")
fig.savefig(here / "overview.png", dpi=160)
plt.close(fig)

for mode, title in passes.items():
    fig, axis = plt.subplots(figsize=(8, 8.5), layout="constrained")
    for i, bench in enumerate(benches):
        for label, offset, color, marker in [("a", -0.16, "#0a6684", "o"), ("b", 0.16, "#bd581b", "s")]:
            key = bench, label, mode
            mid, low, high = stats[key]
            axis.plot([low, high], [i + offset] * 2, c=color, lw=1.4)
            axis.plot(mid, i + offset, marker=marker, ms=5, mec=color,
                      mfc="white" if same_text[key] else color,
                      label=f"Вход {label}" if i == 0 else None)
    scale_note = ""
    if max(abs(x) for b in benches for l in ("a", "b") for x in stats[b, l, mode]) > 45:
        scale_note = "\nЗа пределами ±10% шкала логарифмическая."
        axis.set_xscale("symlog", linthresh=10)
        low, high = axis.get_xlim()
        ticks = [x for x in (-95,-75,-50,-25,-10,-5,0,5,10,25,50,75,100,150,250,500,1000)
                 if low < x < high]
        axis.set_xticks(ticks, [str(x) for x in ticks])
    axis.axvline(0, c="#435260", lw=0.85)
    axis.grid(axis="x", alpha=0.18)
    axis.set_yticks(range(len(benches)), benches.values())
    axis.set_ylim(len(benches) - 0.45, -0.65)
    separators(axis)
    axis.legend(frameon=False, loc="upper center", bbox_to_anchor=(0.5, -0.105), ncols=2, borderaxespad=0)
    axis.set_title(f"{title}   ·   ускорение к −O3, %", weight="bold")
    axis.set_xlabel("Медиана и парный 95%-й интервал. Пустой маркер означает ту же .text, что у −O3." + scale_note, fontsize=9.5)
    fig.savefig(here.parent / "passes" / mode / "results.png", dpi=160)
    plt.close(fig)
