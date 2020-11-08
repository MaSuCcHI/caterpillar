include_directories(catch2) # v2.6.0

file(GLOB_RECURSE FILENAMES *.cpp)

add_executable(run_program ${FILENAMES})
target_link_libraries(run_program caterpillar)
target_compile_definitions(run_program PUBLIC BENCHMARKS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/benchmarks")