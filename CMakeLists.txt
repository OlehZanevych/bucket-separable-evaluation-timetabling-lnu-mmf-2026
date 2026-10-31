cmake_minimum_required(VERSION 3.16)
project(tt_a2_separable_search CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_EXTENSIONS OFF)
# Results are compared bit for bit across machines (the search's acceptance test compares doubles),
# so the compiler must not fuse multiplies and adds into FMA instructions where the hardware has them.
# x86-64 without -march has no FMA either way; on arm64 this keeps the arithmetic the same as there.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffp-contract=off")
find_package(Threads REQUIRED)
file(GLOB CORE_SRC ${CMAKE_SOURCE_DIR}/src/core/*.cpp)
# src/core is the kernel this series shares; src/ctt.cpp is this paper's own, the reader and
# objective of the standard curriculum-based formulation used by Experiment 6.
add_executable(experiments src/main.cpp src/ctt.cpp ${CORE_SRC})
target_include_directories(experiments PRIVATE src)
target_link_libraries(experiments PRIVATE Threads::Threads)
# Off for every recorded run.  On, every re-insertion scan is repeated without pruning and the
# program aborts if the pruned scan missed the minimum (see Worker::scanBest).
option(TT_CHECK_SCAN "check every pruned placement scan against an exhaustive one" OFF)
if(TT_CHECK_SCAN)
  target_compile_definitions(experiments PRIVATE TT_CHECK_SCAN)
endif()
# Off for every recorded run.  On, every scan also replays the pruning rule used before 17 October
# 2026 and counts the scans on which it would have returned a worse placement; `ctt` then prints a
# SCANDIAG line per run.  This is how results/diagnostics/scan-defect.csv was produced.
option(TT_SCAN_DIAG "count the scans the pre-correction pruning rule would have got wrong" OFF)
if(TT_SCAN_DIAG)
  target_compile_definitions(experiments PRIVATE TT_SCAN_DIAG)
endif()
