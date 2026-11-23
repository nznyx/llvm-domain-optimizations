include(../Lexer.cmake)
lexer_source(graphviz_lexer https://gitlab.com/api/v4/projects/4207231/packages/generic/graphviz-releases/12.2.1/graphviz-12.2.1.tar.xz
  85e34b5c982777c30f01dfab9ea7c713b4335a2f584e62c0abb9868413eb915b)
set(lib "${source}/lib")
file(GLOB cdt "${lib}/cdt/*.c")
file(GLOB cgraph "${lib}/cgraph/*.c")
list(REMOVE_ITEM cgraph "${lib}/cgraph/scan.c")
file(CONFIGURE OUTPUT config.h CONTENT "/* cgraph without optional Graphviz components. */\n")
lexer_benchmark("${lib}/cgraph/scan.c" ${cdt} ${cgraph} "${lib}/util/gv_fopen.c")
target_include_directories(graphviz-bench PRIVATE "${lib}" "${lib}/cdt" "${lib}/cgraph" "${CMAKE_CURRENT_BINARY_DIR}")
bench_inputs("${source}/graphs/directed/jsort.gv" "${source}/graphs/directed/awilliams.gv"
  "${source}/graphs/undirected/ngk10_4.gv")
