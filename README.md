# llvm-domain-optimizations

Доменно-специфичные оптимизации LLVM на основе статического анализа горячих и холодных путей. Проект выполняется совместно с Spectral::Technologies.

Для сборки нужны Linux, CMake 3.27+ и LLVM/Clang 18.1 с заголовками. Для Full LTO нужен LLD 18, для Beast — заголовки Boost 1.90.

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DLLVM_DIR="$(llvm-config-18 --cmakedir)" -DBENCHMARK=libyaml -DBENCH_PASS=x86-layout
cmake --build build -j
./build/libyaml-bench 20 input.yaml
```

Выбери бенчмарк через `BENCHMARK=beast|libcsv|libyaml`, проход через `BENCH_PASS=none|error|x86-layout` и уровень оптимизации через `BENCH_OPT_LEVEL=2|3`. Для сравнения используй отдельные каталоги сборки. Базовая сборка для сравнения с `x86-layout` задаётся флагами `-DBENCH_PASS=none -DBENCH_LTO=ON`. Обе сборки используют Full LTO на Linux x86-64.

```sh
./build/beast-bench
./build/libcsv-bench 20 input.csv
./build/libyaml-bench 20 input.yaml
ctest --test-dir build --output-on-failure
```

Запускай исполняемый файл выбранного бенчмарка. Для проверок ErrorHeuristic нужен Python 3. Чтобы запустить проход на готовом LLVM IR, используй `opt-18 -load-pass-plugin=build/ErrorHeuristic.so -passes=error-names,verify -S input.ll -o marked.ll`. [Настройка Beast](bench/beast/README.md) · [Команды для PGO](bench/PGO.md).
