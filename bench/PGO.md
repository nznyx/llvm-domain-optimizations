# PGO

Настрой новый каталог `build-pgo` по корневому README с параметрами `-DBENCH_PASS=none -DBENCH_OPT_LEVEL=3`. Используй ту же настройку LTO, что и в базовой сборке. Подставь исполняемый файл и входные данные выбранного бенчмарка.

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
