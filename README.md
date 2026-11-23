# llvm-domain-optimizations

LLVM-проходы для парсеров. Совместный проект со Spectral::Technologies.

| Проход | Что меняет |
|---|---|
| [error](passes/error/) | Находит проверки ошибок по именам и помечает ветку обработки ошибки как маловероятную. |
| [parser-hot-inline](passes/parser-hot-inline/) | Ищет обработчики байтов и позволяет LLVM встраивать более крупные вызовы на частых путях. |
| [transition-split](passes/transition-split/) | Предвычисляет переходы табличного лексера и ускоряет чтение серий байтов в одном состоянии. |
| [x86-layout](passes/x86-layout/) | На этапе линковки меняет выравнивание ветвлений и внутренних циклов в машинном коде x86-64. |

## Запуск

Нужны Linux x86-64, CMake ≥ 3.27, Python 3 и LLVM 23.1.x с Clang, LLD, compiler-rt и заголовками. Для бенчмарка gzip нужен `gzip`, для ITCH — `patch`. В релизах лексеров уже есть сгенерированные C-файлы.

```sh
export LLVM_CONFIG=llvm-config-23
./bench/run cjson a O3
./bench/run cjson a O3 parser-hot-inline
./bench/run graphviz b O3 transition-split
./bench/run graphviz b O3 transition-split,x86-layout
./bench/run itch b PGO
```

Выберите бенчмарк, вход (`a`, `b` или путь к файлу) и режим (`O2`, `O3` или `PGO`). Четвёртый аргумент задаёт проходы через запятую. PGO обучается на отдельном входе `train`. Все сборки используют Full LTO. Число повторов и флаги компилятора можно передать через `REPEATS` и `BENCH_FLAGS`.

[Что разбирает каждый бенчмарк](bench/README.md) · [Сравнение проходов с O3 и PGO](measurements/README.md)
