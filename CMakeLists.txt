cmake_minimum_required(VERSION 3.8)

project(morphling CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE on)

set(CMAKE_BUILD_TYPE Debug)

add_compile_options(-Wall -g)
# core morphling library
set(CORE_LIB_NAME morphling)

set(SOURCES
  core/smr.cpp
  core/morphling.cpp
)

set(SOURCES
  ${SOURCES}
  loguru/loguru.cpp
)

message("all sources: ${SOURCES}")

include_directories(core)
include_directories(loguru)
add_library(${CORE_LIB_NAME} STATIC ${SOURCES})
target_link_libraries(${CORE_LIB_NAME} dl event)

# app
# add_executable(server app/server.cpp app/utils.cpp)
# set_target_properties(server PROPERTIES LINK_FLAGS -pthread)
# target_link_libraries(server ${CORE_LIB_NAME} event)

# add_executable(server app/server2.cpp app/utils.cpp)
# set_target_properties(server PROPERTIES LINK_FLAGS -pthread)
# target_link_libraries(server ${CORE_LIB_NAME} event)

# add_executable(client app/client.cpp loguru/loguru.cpp)
# set_target_properties(client PROPERTIES LINK_FLAGS -pthread)
# target_link_libraries(client dl)

# add_executable(client2 app/client2.cpp app/utils.cpp)
# set_target_properties(client2 PROPERTIES LINK_FLAGS -pthread)
# target_link_libraries(client2 ${CORE_LIB_NAME} event)

# add_executable(client2 app/client3.cpp app/utils.cpp)
# set_target_properties(client2 PROPERTIES LINK_FLAGS -pthread)
# target_link_libraries(client2 ${CORE_LIB_NAME} event)

# gtest
# after install gtest, /usr/local/lib/cmake/GTest should exist
enable_testing()

set(ALL_TEST_NAME
  entry_vote
  smr_log
  replication
  basic_morphling
)

set(ALL_TEST_SOURCES
  tests/entry_vote_test.cpp
  tests/smr_log_test.cpp
  tests/replication_test.cpp
  tests/basic_morphling_test.cpp
)

find_package(GTest CONFIG REQUIRED)

list(LENGTH ALL_TEST_NAME TEST_CASES_NUM)
message("we have ${TEST_CASES_NUM} tests")
math(EXPR LOOP_STOP "${TEST_CASES_NUM} - 1")

foreach(INDEX RANGE ${LOOP_STOP})
  list(GET ALL_TEST_NAME ${INDEX} TEST_NAME)
  list(GET ALL_TEST_SOURCES ${INDEX} TEST_SOURCES)
  message("${INDEX}: ${TEST_NAME} use source ${TEST_SOURCES}")

  add_executable(${TEST_NAME} ${TEST_SOURCES})

  # ld resolve -pthread, rather than -lpthread
  set_target_properties(${TEST_NAME} PROPERTIES LINK_FLAGS -pthread)
  target_link_libraries(${TEST_NAME} PUBLIC ${CORE_LIB_NAME})
  target_link_libraries(${TEST_NAME} PUBLIC gtest gtest_main)

  add_test(NAME ${TEST_NAME}_test COMMAND ${TEST_NAME})
endforeach()


