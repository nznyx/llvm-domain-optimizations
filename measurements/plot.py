"""Regenerate the overview and pass figures from the paired Linux measurements."""
import csv
from collections import defaultdict
from pathlib import Path
from statistics import median

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

directory = Path(__file__).resolve().parent
inputs = {
    "beast": ("HTTP · Facebook", "HTTP · NetBSD"),
    "fix": ("FIX · биржевой поток", "FIX · сессия 4.1"),
    "gzip": ("gzip · аэропорты", "gzip · полосы"),
    "libcsv": ("CSV · аэропорты", "CSV · полосы"),
    "libyaml": ("YAML · Gateways", "YAML · HTTPRoutes"),
    "pugixml": ("XML · large", "XML · UTF-16"),
}
variants = ["O2", "error", "x86-layout", "error,x86-layout", "PGO"]
rounds = defaultdict(dict)
with (directory / "llvm23.csv").open() as stream:
    rows = list(csv.DictReader(stream))
versions = {row["llvm"] for row in rows}
if len(versions) != 1 or not next(iter(versions)).startswith("23.1."):
    raise ValueError("expected measurements from a single LLVM 23.1.x compiler")
version = versions.pop()
for row in rows:
    group = (row["bench"], row["input"], int(row["round"]))
    variant = row["passes"] if row["passes"] != "none" else row["mode"]
    if variant in rounds[group]:
        raise ValueError(f"duplicate result for {group} {variant}")
    rounds[group][variant] = row

keys = [(bench, label) for bench in inputs for label in ("a", "b")]
labels = [name for pair in inputs.values() for name in pair]
effects = defaultdict(list)
same_text = {}
for bench, label in keys:
    for i in range(1, 13):
        samples = rounds[bench, label, i]
        base = samples["O3"]
        for name in variants:
            row = samples[name]
            assert all(row[k] == base[k] for k in ("input_sha256", "output", "repeats", "cpu"))
            key = bench, label, name
            effects[key].append(100 * (float(base["ms"]) / float(row["ms"]) - 1))
            same_text[key] = row["text_sha256"] == base["text_sha256"]

if __name__ == "__main__":
    fig, axis = plt.subplots(figsize=(7.4, 6.1), layout="constrained")
    limit = max(5, max(abs(median(x)) for x in effects.values()))
    data = [[median(effects[b, l, mode]) for mode in variants] for b, l in keys]
    colors = [[0 if same_text[b, l, mode] else value
               for mode, value in zip(variants, row)] for (b, l), row in zip(keys, data)]
    axis.imshow(colors, cmap="RdBu", vmin=-limit, vmax=limit, aspect="auto")
    axis.set_title(f"LLVM {version}")
    axis.set_xticks(range(5), ["O2", "Error", "Layout", "Оба", "PGO"])
    axis.set_yticks(range(len(keys)), labels)
    axis.tick_params(length=0)
    for i, row in enumerate(data):
        for j, value in enumerate(row):
            b, l = keys[i]
            label = "= O3" if same_text[b, l, variants[j]] else f"{value:+.1f}"
            measured = rounds[b, l, 1]
            if variants[j] == "error,x86-layout" and (
                measured[variants[j]]["text_sha256"] == measured["x86-layout"]["text_sha256"]
            ):
                label = "= Layout"
            axis.text(j, i, label, ha="center", va="center",
                      color="white" if abs(colors[i][j]) > 0.55 * limit else "#17232a")
    fig.suptitle("Ускорение к −O3, %   ·   медиана 12 парных запусков", fontweight="bold")
    fig.savefig(directory/"overview.png", dpi=175)
    plt.close(fig)

    for mode, title in [("error", "ErrorHeuristic"), ("x86-layout", "x86 layout")]:
        fig, axis = plt.subplots(figsize=(8.2, 6.2), layout="constrained")
        color = "#126796"
        for i, (bench, label) in enumerate(keys):
            key = bench, label, mode
            values = sorted(effects[key])
            axis.plot([values[2], values[-3]], [i, i], color=color, linewidth=1.6)
            axis.plot(median(values), i, "o", color=color,
                      mfc="white" if same_text[key] else color, ms=6)
        axis.axvline(0, color="#333333", linewidth=0.9)
        axis.grid(axis="x", alpha=0.2)
        axis.set_yticks(range(len(keys)), labels)
        axis.invert_yaxis()
        axis.set_title(f"{title}   ·   ускорение к −O3   ·   LLVM {version}", fontweight="bold")
        axis.set_xlabel("Изменение скорости, %   ·   отрезок от 3-го до 10-го из 12 замеров")
        fig.savefig(directory.parent/"passes"/mode/"results.png", dpi=175)
        plt.close(fig)
