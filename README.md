# llvm-domain-optimizations

LLVM-проходы для парсеров. Совместный проект со Spectral::Technologies.

Нужны Linux x86-64, CMake ≥ 3.27, Python 3, gzip и LLVM с Clang, LLD, compiler-rt и заголовками. Выбери установленный LLVM и запусти один бенчмарк.

```sh
export LLVM_CONFIG=llvm-config-23
./bench/run fix a O2
./bench/run fix a O3
./bench/run fix a O3 error
./bench/run fix a O3 error,x86-layout
./bench/run fix b PGO
```

Первый аргумент выбирает библиотеку, второй принимает `a`, `b` или путь к файлу. PGO обучается на отдельном входе `train`. Все режимы используют Full LTO. Первая сборка скачивает только зависимости выбранного бенчмарка. На stdout выводятся миллисекунды разбора и контрольные значения.

```sh
REPEATS=50 BENCH_FLAGS=-march=native ./bench/run libyaml ./input.yaml O3 x86-layout
```

[Бенчмарки и входы](bench/README.md) · [ErrorHeuristic](passes/error/README.md) · [x86 layout](passes/x86-layout/README.md) · [Результаты измерений](measurements/README.md)
