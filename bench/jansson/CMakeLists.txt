include(../JSON.cmake)
json_source(jansson akheron/jansson 684e18c927e89615c2d501737e90018f4930d6c5
  3c4fce59bd814a9e63b4a98b4d4d031d4689a994afb87cbe9e5a22c7ff846c7c)

# Have Jansson probe the host and generate its public and private config headers.
set(JANSSON_BUILD_DOCS OFF CACHE BOOL "")
set(JANSSON_EXAMPLES OFF CACHE BOOL "")
set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory("${json_src}" config EXCLUDE_FROM_ALL)
file(GLOB sources "${json_src}/src/*.c")
json_benchmark(${sources})
target_include_directories(jansson-bench PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/config/include"
  "${CMAKE_CURRENT_BINARY_DIR}/config/private_include")
target_compile_definitions(jansson-bench PRIVATE HAVE_CONFIG_H JANSSON_USING_CMAKE)
