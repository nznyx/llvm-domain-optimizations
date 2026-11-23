include(FetchContent)

function(json_source name repository revision sha256)
  FetchContent_Declare(${name}
    URL "https://codeload.github.com/${repository}/tar.gz/${revision}"
    URL_HASH "SHA256=${sha256}" SOURCE_SUBDIR unused)
  FetchContent_MakeAvailable(${name})
  set(json_src "${${name}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(json_benchmark)
  set(target "${BENCHMARK}-bench")
  add_executable(${target} ../main.cpp benchmark.cpp ${ARGN})
  if(ARGN)
    configure_benchmark(${target} ${ARGN})
  else()
    # Header-only parsers are compiled in the adapter's translation unit.
    configure_benchmark(${target} benchmark.cpp)
  endif()

  set(data "https://raw.githubusercontent.com/miloyip/nativejson-benchmark/478d5727c2a4048e835a29c65adecc7d795360d5/data")
  set(dir "${FETCHCONTENT_BASE_DIR}/json-inputs")
  file(MAKE_DIRECTORY "${dir}")
  file(DOWNLOAD "${data}/citm_catalog.json" "${dir}/citm_catalog.json"
    EXPECTED_HASH SHA256=a73e7a883f6ea8de113dff59702975e60119b4b58d451d518a929f31c92e2059 TLS_VERIFY ON)
  file(DOWNLOAD "${data}/canada.json" "${dir}/canada.json"
    EXPECTED_HASH SHA256=f83b3b354030d5dd58740c68ac4fecef64cb730a0d12a90362a7f23077f50d78 TLS_VERIFY ON)
  file(DOWNLOAD "${data}/twitter.json" "${dir}/twitter.json"
    EXPECTED_HASH SHA256=a08b769f32b95f426cbc3abafcec65c1a19d3eb544d4ddf320eae142c99efc5d TLS_VERIFY ON)
  bench_inputs("${dir}/citm_catalog.json" "${dir}/canada.json" "${dir}/twitter.json")
endfunction()
