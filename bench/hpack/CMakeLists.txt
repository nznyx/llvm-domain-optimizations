include(FetchContent)
FetchContent_Declare(nghttp2
  URL https://codeload.github.com/nghttp2/nghttp2/tar.gz/e93691b67a422db1045527c004915e403c50dc7b
  URL_HASH SHA256=bc7754ea0ddf128ed3bd24e23d1466864ae2c8b6cd0701d136da138a95bea64a
  SOURCE_SUBDIR unused)
FetchContent_Declare(hpack_corpus
  URL https://codeload.github.com/http2jp/hpack-test-case/tar.gz/8a1406e7d14bfcb6c046021f13cc15cfb162726d
  URL_HASH SHA256=53355bf708fb890a235b3a679061d116a020818e92caaadbc243f9c91c4c48eb
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(nghttp2 hpack_corpus)
set(lib "${nghttp2_SOURCE_DIR}/lib")
set(PACKAGE_VERSION 1.71.0-DEV)
set(PACKAGE_VERSION_NUM 0x014700)
configure_file("${lib}/includes/nghttp2/nghttp2ver.h.in" nghttp2/nghttp2ver.h @ONLY)

set(sources nghttp2_hd.c nghttp2_hd_huffman.c nghttp2_hd_huffman_data.c
  nghttp2_buf.c nghttp2_helper.c nghttp2_mem.c nghttp2_rcbuf.c)
list(TRANSFORM sources PREPEND "${lib}/")
add_executable(hpack-bench ../main.cpp benchmark.cpp ${sources})
target_include_directories(hpack-bench PRIVATE "${lib}/includes" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(hpack-bench PRIVATE NGHTTP2_STATICLIB HAVE_ARPA_INET_H)
configure_benchmark(hpack-bench ${sources})

find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/stories.py"
  "${hpack_corpus_SOURCE_DIR}/nghttp2" "${PROJECT_BINARY_DIR}/inputs"
  COMMAND_ERROR_IS_FATAL ANY)
