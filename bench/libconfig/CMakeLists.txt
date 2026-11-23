include(../Lexer.cmake)
lexer_source(libconfig_lexer https://hyperrealm.github.io/libconfig/dist/libconfig-1.8.3.tar.gz
  0ed3e37db12e38b6fd7ca85775283412043069da2edcdb0c0ddcf9160a5dcc71)
set(lib "${source}/lib")
lexer_benchmark("${lib}/scanner.c" "${lib}/scanctx.c" "${lib}/strbuf.c" "${lib}/strvec.c"
  "${lib}/util.c" "${lib}/wincompat.c")
target_include_directories(libconfig-bench PRIVATE "${lib}")
bench_inputs("${source}/tests/testdata/nesting.cfg" "${source}/examples/c/example.cfg" "${source}/test.cfg")
