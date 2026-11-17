# PGO

Use a fresh build directory configured as in the root README, with `BENCH_PASS=none` and `BENCH_OPT_LEVEL=3`. Match the LTO setting of the baseline. Replace the executable and inputs for the selected benchmark.

```sh
profile_dir="$(mktemp -d)"
cmake -S . -B build-pgo \
  -DBENCH_CXX_FLAGS="-fprofile-generate=$profile_dir" \
  -DBENCH_LINK_FLAGS=-fprofile-generate
cmake --build build-pgo -j
./build-pgo/libyaml-bench 20 train.yaml
llvm-profdata-18 merge "$profile_dir" -o "$profile_dir/merged.profdata"
cmake -S . -B build-pgo \
  -DBENCH_CXX_FLAGS="-fprofile-use=$profile_dir/merged.profdata" -DBENCH_LINK_FLAGS=
cmake --build build-pgo -j
./build-pgo/libyaml-bench 20 test.yaml
```
