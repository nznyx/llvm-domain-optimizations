# Boost.Beast

При настройке проекта укажи `-DBENCHMARK=beast`. CMake скачает закреплённую версию Beast. Для распакованных заголовков Boost 1.90 укажи `-DBOOST_HEADERS=/path/to/boost_1_90_0`.

```sh
cmake --build build --target beast-bench -j
./build/beast-bench
```

Штатный бенчмарк выводит время разбора отдельно для Beast и Node.js. Чтобы включить правила ErrorHeuristic для Beast, добавь `-DBEAST_ERRORS=ON`.
