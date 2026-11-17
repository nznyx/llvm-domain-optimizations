set(BOOST_HEADERS "" CACHE PATH "Directory containing the boost headers")
if(NOT BOOST_HEADERS)
  find_package(Boost 1.90.0 EXACT REQUIRED CONFIG)
  set(BOOST_HEADERS "${Boost_INCLUDE_DIRS}")
endif()
include(FetchContent)

set(BEAST_ARCHIVE "" CACHE FILEPATH "Use a local Beast archive instead of downloading")
set(BEAST_URL "https://codeload.github.com/boostorg/beast/tar.gz/b4160b21f8aa9e5305d8befd5051bbfbda5eaef8")
if(BEAST_ARCHIVE)
  set(BEAST_URL "${BEAST_ARCHIVE}")
endif()
FetchContent_Populate(beast
  URL "${BEAST_URL}"
  URL_HASH SHA256=fab61fe799b6bbec2edfb6c8a231c4dd933841014b99d85ac9f65336d75e8ca3
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(beast_SOURCE_DIR "${beast_SOURCE_DIR}" PARENT_SCOPE)
set(Boost_INCLUDE_DIRS "${BOOST_HEADERS}" PARENT_SCOPE)


add_executable(beast-bench
  "${beast_SOURCE_DIR}/test/bench/parser/bench_parser.cpp"
  "${beast_SOURCE_DIR}/test/bench/parser/nodejs_parser.cpp"
  "${beast_SOURCE_DIR}/test/lib_test.cpp")
target_include_directories(beast-bench PRIVATE "${beast_SOURCE_DIR}/include" "${beast_SOURCE_DIR}" "${BOOST_HEADERS}")
configure_benchmark(beast-bench)
