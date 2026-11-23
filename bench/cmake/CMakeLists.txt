include(../Lexer.cmake)
lexer_source(cmake_lexer https://codeload.github.com/Kitware/CMake/tar.gz/refs/tags/v3.31.6
  a325dc0566c1421c611dd7507dabd2706419081af8126273dc436d4b1066873c)

set(KWSYS_NAMESPACE cmsys)
set(KWSYS_NAME_IS_KWSYS 0)
set(KWSYS_BUILD_SHARED 0)
configure_file("${source}/Source/kwsys/Configure.h.in" cmsys/Configure.h @ONLY)
configure_file("${source}/Utilities/cmThirdParty.h.in" cmThirdParty.h)

lexer_benchmark("${source}/Source/LexerParser/cmListFileLexer.c")
target_include_directories(cmake-bench PRIVATE "${source}/Source" "${source}/Utilities" "${CMAKE_CURRENT_BINARY_DIR}")
bench_inputs("${source}/CMakeLists.txt" "${source}/Tests/CMakeLists.txt" "${source}/Modules/FindBoost.cmake")
