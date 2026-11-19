include(FetchContent)
FetchContent_Declare(hffix
  URL https://codeload.github.com/jamesdbrock/hffix/tar.gz/797ce7db0bd1c240acee88fad297b69bec428778
  URL_HASH SHA256=5a082b822fa22bc41df48d3db274832eabf709c57f637aeccd14d5d1dbc3da94
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(hffix)
add_executable(fix-bench ../main.cpp benchmark.cpp)
target_include_directories(fix-bench PRIVATE "${hffix_SOURCE_DIR}/include")
configure_benchmark(fix-bench benchmark.cpp)

set(data "${hffix_SOURCE_DIR}/test/data")
bench_inputs("${data}/fix.5.0.set.2" "${data}/fix.5.0.set.1" "${data}/fix.4.1.set.1")
