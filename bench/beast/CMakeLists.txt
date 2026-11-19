include(FetchContent)
set(BOOST_HEADERS "" CACHE PATH "Existing Boost 1.90 include directory")
if(NOT BOOST_HEADERS)
  FetchContent_Declare(boost_headers
    URL https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.bz2
    URL_HASH SHA256=49551aff3b22cbc5c5a9ed3dbc92f0e23ea50a0f7325b0d198b705e8ee3fc305
    SOURCE_SUBDIR unused)
  FetchContent_MakeAvailable(boost_headers)
  set(BOOST_HEADERS "${boost_headers_SOURCE_DIR}")
endif()
set(BEAST_ARCHIVE "" CACHE FILEPATH "Existing Beast archive")
set(beast_url https://codeload.github.com/boostorg/beast/tar.gz/b4160b21f8aa9e5305d8befd5051bbfbda5eaef8)
if(BEAST_ARCHIVE)
  set(beast_url "${BEAST_ARCHIVE}")
endif()
FetchContent_Declare(beast URL "${beast_url}"
  URL_HASH SHA256=fab61fe799b6bbec2edfb6c8a231c4dd933841014b99d85ac9f65336d75e8ca3
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(beast)
set(beast_SOURCE_DIR "${beast_SOURCE_DIR}" PARENT_SCOPE)
set(Boost_INCLUDE_DIRS "${BOOST_HEADERS}" PARENT_SCOPE)

add_executable(beast-bench ../main.cpp benchmark.cpp)
# Keep the original upstream benchmark available explicitly.
add_executable(beast-upstream EXCLUDE_FROM_ALL
  "${beast_SOURCE_DIR}/test/bench/parser/bench_parser.cpp"
  "${beast_SOURCE_DIR}/test/bench/parser/nodejs_parser.cpp" "${beast_SOURCE_DIR}/test/lib_test.cpp")
foreach(target beast-bench beast-upstream)
  target_include_directories(${target} PRIVATE "${beast_SOURCE_DIR}/include" "${beast_SOURCE_DIR}" "${BOOST_HEADERS}")
endforeach()
configure_benchmark(beast-bench benchmark.cpp)
get_target_property(upstream_sources beast-upstream SOURCES)
configure_benchmark(beast-upstream ${upstream_sources})

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(qifs https://raw.githubusercontent.com/qpackers/qifs/da52cd936b3e34dac7ac12aa8066fc57676af106/qifs)
function(http_input name hash)
  set(qif "${FETCHCONTENT_BASE_DIR}/${name}.qif")
  file(DOWNLOAD "${qifs}/${name}.qif" "${qif}" EXPECTED_HASH SHA256=${hash} TLS_VERIFY ON)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/from_qif.py"
    "${qif}" "${CMAKE_CURRENT_BINARY_DIR}/${name}.http" ${ARGN} COMMAND_ERROR_IS_FATAL ANY)
endfunction()
http_input(netbsd 5a09b7cd4b0ce902a8b4e141ea9e0e4a1e0f9891ebef72e8dcd9505198916ec3)
http_input(fb-req 75b501df8290c6615b0e626527c64ccf19c53c813c52e841004e558db947642b
           "${CMAKE_CURRENT_BINARY_DIR}/train.http")
bench_inputs("${CMAKE_CURRENT_BINARY_DIR}/train.http" "${CMAKE_CURRENT_BINARY_DIR}/fb-req.http" "${CMAKE_CURRENT_BINARY_DIR}/netbsd.http")
