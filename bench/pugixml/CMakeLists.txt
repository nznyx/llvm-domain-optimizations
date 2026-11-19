include(FetchContent)
FetchContent_Declare(pugixml
  URL https://codeload.github.com/zeux/pugixml/tar.gz/db78afc2b7d8f043b4bc6b185635d949ea2ed2a8
  URL_HASH SHA256=56e43bbe2733cc149c7d50a6d11a65b3c4ed080b16bfa6050154e09b1c320ed0
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(pugixml)
add_executable(pugixml-bench ../main.cpp benchmark.cpp "${pugixml_SOURCE_DIR}/src/pugixml.cpp")
target_include_directories(pugixml-bench PRIVATE "${pugixml_SOURCE_DIR}/src")
configure_benchmark(pugixml-bench "${pugixml_SOURCE_DIR}/src/pugixml.cpp")

set(samples "${pugixml_SOURCE_DIR}/docs/samples")
bench_inputs("${samples}/tree.xml" "${pugixml_SOURCE_DIR}/tests/data/large.xml" "${samples}/weekly-utf-16.xml")
