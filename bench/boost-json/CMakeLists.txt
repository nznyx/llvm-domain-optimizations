include(../JSON.cmake)
set(BOOST_HEADERS "" CACHE PATH "Existing Boost 1.90 include directory")
if(NOT BOOST_HEADERS)
  FetchContent_Declare(boost_headers
    URL https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.bz2
    URL_HASH SHA256=49551aff3b22cbc5c5a9ed3dbc92f0e23ea50a0f7325b0d198b705e8ee3fc305
    SOURCE_SUBDIR unused)
  FetchContent_MakeAvailable(boost_headers)
  set(BOOST_HEADERS "${boost_headers_SOURCE_DIR}")
endif()
json_benchmark()
target_include_directories(boost-json-bench PRIVATE "${BOOST_HEADERS}")
