include(../JSON.cmake)
json_source(picojson kazuho/picojson 111c9be5188f7350c2eac9ddaedd8cca3d7bf394
  671f89832a17e9e71398f80c0a326afa2ebe81f4c26d5a9992e1fcd0888ae151)
json_benchmark()
target_include_directories(picojson-bench PRIVATE "${json_src}")
