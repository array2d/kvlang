find_package(Threads REQUIRED)

# -- 01: basic alloc/free with reuse --
add_executable(01_basic_alloc_free 01_basic_alloc_free.c)
target_link_libraries(01_basic_alloc_free PRIVATE blockmalloc)

# -- 02: alloc until OOM --
add_executable(02_alloc_max 02_alloc_max.c)
target_link_libraries(02_alloc_max PRIVATE blockmalloc)

# -- 03: multi-threaded concurrent alloc/free --
add_executable(03_multithread 03_multithread.c)
target_link_libraries(03_multithread PRIVATE blockmalloc Threads::Threads)

# -- 04: multi-process concurrent alloc/free (shared memory) --
add_executable(04_multiprocess 04_multiprocess.c)
target_link_libraries(04_multiprocess PRIVATE blockmalloc)

# -- 05: multi-process, each process multi-threaded --
add_executable(05_multiprocess_multithread 05_multiprocess_multithread.c)
target_link_libraries(05_multiprocess_multithread PRIVATE blockmalloc Threads::Threads)

add_test(NAME 01_basic_alloc_free COMMAND 01_basic_alloc_free)
add_test(NAME 02_alloc_max COMMAND 02_alloc_max)
add_test(NAME 03_multithread COMMAND 03_multithread)
add_test(NAME 04_multiprocess COMMAND 04_multiprocess)
add_test(NAME 05_multiprocess_multithread COMMAND 05_multiprocess_multithread)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(01_basic_alloc_free PRIVATE ENABLE_LOG)
    target_compile_definitions(02_alloc_max PRIVATE ENABLE_LOG)
    target_compile_definitions(03_multithread PRIVATE ENABLE_LOG)
    target_compile_definitions(04_multiprocess PRIVATE ENABLE_LOG)
    target_compile_definitions(05_multiprocess_multithread PRIVATE ENABLE_LOG)
endif()
