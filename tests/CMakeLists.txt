cmake_minimum_required (VERSION 2.8)

#### USER DEFINED ##############################################################

project("utils_data_structure_test")

#### BASIC SETTINGS ############################################################

include_directories(${PROJECT_SOURCE_DIR})

set (CMAKE_CXX_FLAGS "-std=c++17 -Wall -Wextra -O3 -march=native -g -pthread -static-libstdc++ -Wno-parentheses -Wno-unused-but-set-parameter")

#### FIND PACKAGES #############################################################

find_package(Threads REQUIRED)
set(LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

if (QFILTER_HASHFCT STREQUAL XXHASH)
  message(STATUS "Looking for xxhash")

  find_path(XXHASH_DIR xxHash/xxhash.h
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/misc/)

  if (XXHASH_DIR)
    include_directories(${XXHASH_DIR}/xxHash)
    message(STATUS "Looking for xxhash -- found")
  else()
    message(STATUS "Looking for xxhash -- not found")
    message("Cannot find xxHash. Use MURMUR2 instead.")
    set(QFILTER_HASHFCT MURMUR2)
  endif()
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

#### MAKE TESTS ################################################################

function( CompileTest sources name )
  add_executable(${name} ${sources})
  #target_compile_definitions(${name} PRIVATE -D ${variant} -D ${HASH_FCT})
  target_link_libraries(${name} ${LIBRARIES})
endfunction( CompileTest )

CompileTest( src/test_circular_buffer.cpp circular_buffer_test)
CompileTest( src/test_many_producer_single_consumer_buffer.cpp many_producer_single_consumer_buffer_test)
CompileTest( src/test_reclamation_strategies.cpp reclamation_test)
