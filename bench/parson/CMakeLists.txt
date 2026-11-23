include(../JSON.cmake)
json_source(parson kgabis/parson ec53fb6528b45811df9db0db22cab96a94a96a11
  9ca55b19539a019dd86234543a965be4533be600a8d2539d0046322888df4a41)
json_benchmark("${json_src}/parson.c")
target_include_directories(parson-bench PRIVATE "${json_src}")
