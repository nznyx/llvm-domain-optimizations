include(FetchContent)
FetchContent_Declare(zlib
  URL https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.3.1
  URL_HASH SHA256=17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(zlib)
set(sources adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c)
list(TRANSFORM sources PREPEND "${zlib_SOURCE_DIR}/")
add_executable(gzip-bench ../main.cpp benchmark.cpp ${sources})
target_include_directories(gzip-bench PRIVATE "${zlib_SOURCE_DIR}")
configure_benchmark(gzip-bench ${sources})

find_program(GZIP gzip REQUIRED)
foreach(input navaids airports runways)
  set(csv "${CMAKE_CURRENT_SOURCE_DIR}/../libcsv/data/${input}.csv")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${csv}")
  execute_process(COMMAND "${GZIP}" -n -6 -c "${csv}"
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${input}.gz" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
bench_inputs("${CMAKE_CURRENT_BINARY_DIR}/navaids.gz" "${CMAKE_CURRENT_BINARY_DIR}/airports.gz"
             "${CMAKE_CURRENT_BINARY_DIR}/runways.gz")
