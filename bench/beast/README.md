# Boost.Beast

Configure the root project with `-DBENCHMARK=beast`. CMake downloads the pinned Beast source. If using unpacked Boost 1.90 headers, pass `-DBOOST_HEADERS=/path/to/boost_1_90_0`.

```sh
cmake --build build --target beast-bench -j
./build/beast-bench
```

The upstream benchmark prints separate Beast and Node.js parser times. Enable `-DBEAST_ERRORS=ON` to include the optional Beast-specific rules when compiling with ErrorHeuristic.
