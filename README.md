# llvm-domain-optimizations

Domain-specific LLVM optimizations through static hot/cold path analysis, in collaboration with Spectral::Technologies.

Linux, CMake 3.27+, LLVM/Clang 18.1 with development headers, LLD 18 for Full LTO. Beast also needs Boost 1.90 headers.

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DLLVM_DIR="$(llvm-config-18 --cmakedir)" -DBENCHMARK=libyaml -DBENCH_PASS=x86-layout
cmake --build build -j
./build/libyaml-bench 20 input.yaml
```

Choose `BENCHMARK=beast|libcsv|libyaml`, `BENCH_PASS=none|error|x86-layout` and `BENCH_OPT_LEVEL=2|3`. Use separate build directories for comparisons. For the x86 layout baseline use `BENCH_PASS=none -DBENCH_LTO=ON`. Both layout builds use Full LTO on Linux x86-64.

```sh
./build/beast-bench
./build/libcsv-bench 20 input.csv
./build/libyaml-bench 20 input.yaml
ctest --test-dir build --output-on-failure
```

Run the executable for the selected benchmark. ErrorHeuristic checks use Python 3. For a standalone IR run, use `opt-18 -load-pass-plugin=build/ErrorHeuristic.so -passes=error-names,verify -S input.ll -o marked.ll`. See [Beast setup](bench/beast/README.md) and [PGO commands](bench/PGO.md).
