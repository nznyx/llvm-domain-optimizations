include(FetchContent)
FetchContent_Declare(libyaml
  URL https://codeload.github.com/yaml/libyaml/tar.gz/2c891fc7a770e8ba2fec34fc6b545c672beb37e6
  URL_HASH SHA256=77987051b6f5bee8ba472f1efbe0053663a58d8d3e7a2f0f4a27d178adbf8844
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(libyaml)

set(YAML_VERSION_MAJOR 0)
set(YAML_VERSION_MINOR 2)
set(YAML_VERSION_PATCH 5)
set(YAML_VERSION_STRING "0.2.5")
configure_file("${libyaml_SOURCE_DIR}/cmake/config.h.in" config.h)
set(sources api.c dumper.c emitter.c loader.c parser.c reader.c scanner.c writer.c)
list(TRANSFORM sources PREPEND "${libyaml_SOURCE_DIR}/src/")
add_executable(libyaml-bench benchmark.cpp ${sources})
target_include_directories(libyaml-bench PRIVATE "${libyaml_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(libyaml-bench PRIVATE HAVE_CONFIG_H YAML_DECLARE_STATIC)
configure_benchmark(libyaml-bench ${sources})

set(crds https://raw.githubusercontent.com/kubernetes-sigs/gateway-api/v1.2.0/config/crd/standard)
file(DOWNLOAD "${crds}/gateway.networking.k8s.io_gateways.yaml" "${FETCHCONTENT_BASE_DIR}/gateways.yaml"
     EXPECTED_HASH SHA256=8a8f1c3f0e54c667ad68982ad1d98ff3397fc4ce19917efc19ae2c76bda61cb7 TLS_VERIFY ON)
file(DOWNLOAD "${crds}/gateway.networking.k8s.io_httproutes.yaml" "${FETCHCONTENT_BASE_DIR}/httproutes.yaml"
     EXPECTED_HASH SHA256=f8aff76af966cadfbf505776a5fb9f54f9ea3dccf3783c51468318e30d779416 TLS_VERIFY ON)
bench_inputs("${libyaml_SOURCE_DIR}/.travis.yml"
             "${FETCHCONTENT_BASE_DIR}/gateways.yaml" "${FETCHCONTENT_BASE_DIR}/httproutes.yaml")
