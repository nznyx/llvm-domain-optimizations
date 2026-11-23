include(../JSON.cmake)
json_source(cjson DaveGamble/cJSON c859b25da02955fef659d658b8f324b5cde87be3
  880a86445ad5cf38f877bac31198268d8fac5d2ce996ebf55d3066a4109ddd8d)
json_benchmark("${json_src}/cJSON.c")
target_include_directories(cjson-bench PRIVATE "${json_src}")
