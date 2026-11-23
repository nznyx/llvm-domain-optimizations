include(FetchContent)

function(lexer_source name url sha256)
  FetchContent_Declare(${name} URL "${url}" URL_HASH "SHA256=${sha256}" SOURCE_SUBDIR unused)
  FetchContent_MakeAvailable(${name})
  set(source "${${name}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

# Only the generated scanner gets the compiler plugin. Its ordinary runtime
# (for example, jq's string values) and our token consumer keep the same flags.
function(lexer_benchmark scanner)
  add_executable(${BENCHMARK}-bench ../main.cpp ../lexer.cpp scan.c "${scanner}" ${ARGN})
  configure_benchmark(${BENCHMARK}-bench "${scanner}")
endfunction()
