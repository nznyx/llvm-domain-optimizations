include(FetchContent)
FetchContent_Declare(libcsv
  URL https://codeload.github.com/rgamble/libcsv/tar.gz/b1d5212831842ee5869d99bc208a21837e4037d5
  URL_HASH SHA256=328ccd918edeeeea52164f7c43500d3ce2f039594c0c81b0aa2e6041bce88ec4
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libcsv)

add_executable(libcsv-bench benchmark.cpp "${libcsv_SOURCE_DIR}/libcsv.c")
target_include_directories(libcsv-bench PRIVATE "${libcsv_SOURCE_DIR}")
configure_benchmark(libcsv-bench)
