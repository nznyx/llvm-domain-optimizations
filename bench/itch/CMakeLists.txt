include(FetchContent)
FetchContent_Declare(cpptrader
  URL https://codeload.github.com/chronoxor/CppTrader/tar.gz/3815893630abe5cd2281c9fd3c6775808820b763
  URL_HASH SHA256=43c954dd801ab8c0fbe84b6fcc680b0d8924c2be738abc6d2df215968987fe3b
  PATCH_COMMAND patch --forward -p1 -i "${CMAKE_CURRENT_SOURCE_DIR}/timestamp.patch"
  SOURCE_SUBDIR unused)
FetchContent_Declare(cppcommon
  URL https://codeload.github.com/chronoxor/CppCommon/tar.gz/9eba9ba6034a2b408a50fb84593adb8730e5e954
  URL_HASH SHA256=3b942fe8b99da8ed9f3e4ac2faff2be0ae2ffed31733dd9ac4302e2444ddf846
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(cpptrader cppcommon)
set(parser "${cpptrader_SOURCE_DIR}/source/trader/providers/nasdaq/itch_handler.cpp")
add_executable(itch-bench ../main.cpp benchmark.cpp "${parser}")
target_include_directories(itch-bench SYSTEM PRIVATE "${cpptrader_SOURCE_DIR}/include" "${cppcommon_SOURCE_DIR}/include")
target_compile_definitions(itch-bench PRIVATE NDEBUG)
configure_benchmark(itch-bench "${parser}")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/capture.py"
  "${FETCHCONTENT_BASE_DIR}/nasdaq-prefix.gz" "${PROJECT_BINARY_DIR}/inputs"
  COMMAND_ERROR_IS_FATAL ANY)
