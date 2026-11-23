include(../JSON.cmake)
json_source(json11 dropbox/json11 2df9473fb3605980db55ecddf34392a2e832ad35
  59d65d8a15df1494d8f26c034f0d8fc89a18d5e376c4a23fab0705ca2ad7867f)
json_benchmark("${json_src}/json11.cpp")
target_include_directories(json11-bench PRIVATE "${json_src}")
target_compile_definitions(json11-bench PRIVATE BENCH_STRING_INPUT)
