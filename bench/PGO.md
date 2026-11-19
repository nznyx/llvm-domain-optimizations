# PGO

```sh
./bench/run fix b PGO
```

Команда собирает парсер с `-fprofile-generate`, разбирает `train`, объединяет счётчики через `llvm-profdata merge` и собирает `-O3 -fprofile-use`. Затем запускает выбранный вход. Сравнение для того же файла выполняет `./bench/run fix b O3`.
