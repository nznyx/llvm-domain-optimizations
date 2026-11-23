# Бенчмарки

В командах `./bench/run ИМЯ a O3` аргумент `a` можно заменить на `b` или путь к своему файлу. Вход `train` используется для обучения PGO.

| Имя | Что разбирает | Входы a / b |
|---|---|---|
| `beast` | Boost.Beast читает заголовки запросов HTTP/1.1. | Facebook / NetBSD |
| `fix` | hffix обходит поля биржевых FIX-сообщений и проверяет контрольную сумму. | FIXT 1.1 / сессия FIX 4.1 |
| `itch` | CppTrader декодирует сообщения Nasdaq TotalView-ITCH 5.0, включая тип, время события и поля сообщения. | Два непересекающихся фрагмента торгового дня 30.01.2019 |
| `hpack` | nghttp2 восстанавливает поля заголовков HTTP/2 из сжатых блоков HPACK. Динамическая таблица сохраняется между блоками одной истории. | Истории nghttp2 № 20 / 30 |
| `qpack` | nghttp3 восстанавливает заголовки HTTP/3 из записей QPACK, в том числе с обновлениями таблицы от кодировщика. | Запросы Facebook / NetBSD |
| `gzip` | zlib распаковывает gzip-поток и проверяет CRC. | Аэропорты / взлётные полосы |
| `libcsv` | libcsv читает поля и строки CSV. | Аэропорты / взлётные полосы |
| `libyaml` | libyaml выдаёт события YAML при чтении манифестов Kubernetes. | Gateways / HTTPRoutes |
| `pugixml` | pugixml строит дерево XML-документа. | large.xml / weekly-utf-16.xml |
| `cjson`, `parson`, `picojson`, `json11`, `jansson`, `boost-json` | Шесть библиотек строят и освобождают дерево одного JSON-документа. Проверочное значение учитывает число элементов корня. | Геометрия Канады (GeoJSON) / лента Twitter |
| `cmake` | Лексер CMake читает токены CMakeLists.txt и модулей .cmake. | Tests/CMakeLists.txt / FindBoost.cmake |
| `graphviz` | Лексер Graphviz читает токены ориентированных и неориентированных DOT-графов. | awilliams.gv / ngk10_4.gv |
| `jq` | Лексер jq читает исходный текст программ на языке jq. | builtin.jq / utf8-truncate.jq |
| `libconfig` | Лексер libconfig читает имена, значения и знаки в файлах конфигурации. | example.cfg / test.cfg |

Лексеры возвращают контрольное значение по типам и длинам токенов. Время охватывает лексический разбор до конца файла, без построения AST. Для сетевых форматов используются двубайтовые длины сообщений ITCH, четырёхбайтовые длины блоков HPACK и формат записей QPACK interop. CppTrader собирается с исправлением чтения шестибайтовой метки времени во всех режимах. Исходники, входы и их контрольные суммы закреплены в каталогах бенчмарков. [Nasdaq ITCH](https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/) · [HPACK](https://github.com/http2jp/hpack-test-case) · [QPACK](https://github.com/qpackers/qifs) · [JSON](https://github.com/miloyip/nativejson-benchmark/tree/478d5727c2a4048e835a29c65adecc7d795360d5/data)

Входные CSV — срезы OurAirports по 512 записей, распространяемые как public domain. Gzip использует те же байты. [Источник и лицензия](https://ourairports.com/data/)
