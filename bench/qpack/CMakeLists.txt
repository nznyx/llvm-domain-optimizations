include(FetchContent)
FetchContent_Declare(nghttp3
  URL https://codeload.github.com/ngtcp2/nghttp3/tar.gz/4364fb1bd3ebeb3951df961a43ff22225aae0878
  URL_HASH SHA256=5925b6fbc25e11ecb3763d02083c3a74daf68ac230c15edda351ed873bca34f3
  SOURCE_SUBDIR unused)
FetchContent_Declare(qifs
  URL https://codeload.github.com/qpackers/qifs/tar.gz/da52cd936b3e34dac7ac12aa8066fc57676af106
  URL_HASH SHA256=2c0c04bab6ff5b8c93a6a7ce1a4fdda5f6ef158b2e1ad112e7dbca26125f3f47
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(nghttp3 qifs)
set(lib "${nghttp3_SOURCE_DIR}/lib")
set(PACKAGE_VERSION 1.20.0-DEV)
set(PACKAGE_VERSION_NUM 0x011400)
configure_file("${lib}/includes/nghttp3/version.h.in" nghttp3/version.h @ONLY)

set(sources nghttp3_qpack.c nghttp3_qpack_huffman.c nghttp3_qpack_huffman_data.c
  nghttp3_mem.c nghttp3_buf.c nghttp3_rcbuf.c nghttp3_ringbuf.c nghttp3_map.c
  nghttp3_ksl.c nghttp3_pq.c nghttp3_opl.c nghttp3_balloc.c nghttp3_objalloc.c nghttp3_str.c
  nghttp3_conv.c nghttp3_unreachable.c nghttp3_err.c)
list(TRANSFORM sources PREPEND "${lib}/")
set(examples "${nghttp3_SOURCE_DIR}/examples")
add_executable(qpack-bench ../main.cpp benchmark.cpp ${sources}
  "${examples}/qpack_decode.cc" "${examples}/util.cc")
target_include_directories(qpack-bench PRIVATE "${lib}/includes" "${examples}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(qpack-bench PRIVATE NGHTTP3_STATICLIB HAVE_ARPA_INET_H)
configure_benchmark(qpack-bench ${sources} "${examples}/qpack_decode.cc")

set(data "${qifs_SOURCE_DIR}/encoded/qpack-06/nghttp3")
bench_inputs("${data}/fb-resp.out.4096.100.1" "${data}/fb-req.out.4096.100.1" "${data}/netbsd.out.4096.100.1")
