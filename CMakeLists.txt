cmake_minimum_required(VERSION 3.16)
project(amanda C)
set(CMAKE_C_STANDARD 11)

if(EXISTS "${CMAKE_SOURCE_DIR}/version.bin")
  file(READ "${CMAKE_SOURCE_DIR}/version.bin" AVER)
  string(STRIP "${AVER}" AVER)
else()
  set(AVER "1.0.1")
endif()

file(WRITE "${CMAKE_SOURCE_DIR}/include/version_gen.h"
  "#ifndef AMANDA_VERSION_GEN_H\n#define AMANDA_VERSION_GEN_H\n#define AMANDA_VERSION_GEN \"${AVER}\"\n#endif\n")

add_executable(amandac
  src/amanda.c src/utils.c src/pdf_extractor.c src/chunker.c
  src/embedder.c src/question_gen.c src/decision_engine.c src/laya_backend.c
  src/packager.c src/server.c src/eval.c src/calibra.c src/config.c src/cli.c src/main.c)
target_include_directories(amandac PRIVATE include)

if(WIN32)
  target_link_libraries(amandac ws2_32)
endif()

if(UNIX)
  target_link_libraries(amandac m)
endif()

enable_testing()
add_executable(amanda_tests tests/test_all.c
  src/amanda.c src/utils.c src/pdf_extractor.c src/chunker.c
  src/embedder.c src/question_gen.c src/decision_engine.c src/laya_backend.c src/packager.c src/eval.c src/calibra.c src/config.c)
target_include_directories(amanda_tests PRIVATE include)
if(WIN32)
  target_link_libraries(amanda_tests ws2_32)
endif()
if(UNIX)
  target_link_libraries(amanda_tests m)
endif()
add_test(NAME amanda_unit COMMAND amanda_tests)

# Paridade com build.bat (Windows): incrementa version.bin sob demanda.
# Nao roda no build padrao (CI fica reproduzivel).
# Uso: cmake --build build --target version_bump
add_custom_target(version_bump
  COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/scripts/bump_version.cmake"
  COMMENT "Incrementando version.bin"
  VERBATIM)
