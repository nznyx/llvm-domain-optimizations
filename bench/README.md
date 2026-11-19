# Бенчмарки

| Аргумент | Что измеряем | Обучение PGO | Вход a | Вход b |
|---|---|---|---|---|
| `beast` | Boost.Beast, заголовки HTTP/1.1 | первые 76 запросов Facebook | остальные 307 Facebook | NetBSD QIF |
| `fix` | hffix, обход полей и проверка checksum | FIXT 1.1 set 2 | биржевой FIXT 1.1 set 1, 2 МБ | сессия FIX 4.1 |
| `gzip` | zlib, распаковка одного gzip-потока с проверкой CRC | радиомаяки | аэропорты | взлётные полосы |
| `libcsv` | libcsv, потоковые вызовы для полей и строк | радиомаяки | аэропорты | взлётные полосы |
| `libyaml` | libyaml, чтение YAML-событий | конфигурация CI libyaml | Kubernetes Gateways | Kubernetes HTTPRoutes |
| `pugixml` | построение XML DOM | tree.xml | large.xml | weekly-utf-16.xml |

Каждый вход читается в память до замера. Библиотека сама управляет внутренними буферами. В gzip драйвер передаёт выход частями по 32 КиБ. Размер входного файла, число внутренних повторов и работа парсера различаются, поэтому время сравнивается внутри строки и входа.

FIX и XML входят в закреплённые исходники библиотек. Для HTTP пары полей QIF записываются как HTTP/1.1 с сохранением порядка и значений заголовков. YAML-манифесты закреплены по версии и SHA-256. [hffix](https://github.com/jamesdbrock/hffix/tree/797ce7db0bd1c240acee88fad297b69bec428778/test/data) · [QIF](https://github.com/qpackers/qifs/tree/da52cd936b3e34dac7ac12aa8066fc57676af106/qifs) · [Gateway API](https://github.com/kubernetes-sigs/gateway-api/tree/v1.2.0/config/crd/standard) · [pugixml](https://github.com/zeux/pugixml/tree/db78afc2b7d8f043b4bc6b185635d949ea2ed2a8)

Три CSV содержат заголовок и первые 512 записей каждого файла OurAirports. Эти срезы лежат в репозитории. Gzip использует те же байты, сжатые с `gzip -n -6`. OurAirports публикует данные как public domain. [Источник и лицензия](https://ourairports.com/data/)

`error` подключён к исходникам библиотеки. У header-only Beast и hffix это файл с вызовами парсера. Чтение аргументов, файлов и таймер вынесены из области прохода. `x86-layout` работает при финальной компоновке всей программы.

Готовый бинарник можно запускать напрямую, например `./build/23.1.6/fix/O3-error/fix-bench 50 ./messages.fix`. Для нового парсера добавь каталог в `bench/`, передай его исходники в `configure_benchmark` и укажи три файла через `bench_inputs`. Короткий пример есть в [bench/fix](fix/CMakeLists.txt).
