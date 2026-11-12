find_package(Threads REQUIRED)

# -- 01: basic init, alloc, free --
add_executable(01_basic 01_basic.c)
target_link_libraries(01_basic slotsboxmalloc)

# -- 02: bench: 111 alloc/write/read/free --
add_executable(02_bench 02_bench.c)
target_link_libraries(02_bench slotsboxmalloc)

# -- 03: stress: alloc until full, random free/alloc loop --
add_executable(03_stress 03_stress.c)
target_link_libraries(03_stress slotsboxmalloc)

# -- 04: multi-threaded concurrent alloc/free --
add_executable(04_multithread 04_multithread.c)
target_link_libraries(04_multithread slotsboxmalloc Threads::Threads)

# -- 05: multi-process concurrent alloc/free (shared memory) --
add_executable(05_multiprocess 05_multiprocess.c)
target_link_libraries(05_multiprocess slotsboxmalloc)

# -- 06: multi-process, each process multi-threaded --
add_executable(06_multiprocess_multithread 06_multiprocess_multithread.c)
target_link_libraries(06_multiprocess_multithread slotsboxmalloc Threads::Threads)

add_test(NAME 01_basic COMMAND 01_basic)
add_test(NAME 02_bench COMMAND 02_bench)
add_test(NAME 03_stress COMMAND 03_stress)
add_test(NAME 04_multithread COMMAND 04_multithread)
add_test(NAME 05_multiprocess COMMAND 05_multiprocess)
# -- 07: concurrent throughput benchmark --
add_executable(07_bench_concurrent 07_bench_concurrent.c)
target_link_libraries(07_bench_concurrent slotsboxmalloc Threads::Threads)

add_test(NAME 06_multiprocess_multithread COMMAND 06_multiprocess_multithread)
# -- 08: pure alloc/free throughput (no data writes) --
add_executable(08_bench_pure 08_bench_pure.c)
target_link_libraries(08_bench_pure slotsboxmalloc Threads::Threads)

add_test(NAME 07_bench_concurrent COMMAND 07_bench_concurrent)
# -- 09: sharded instances (ideal linear scaling reference) --
add_executable(09_bench_sharded 09_bench_sharded.c)
target_link_libraries(09_bench_sharded slotsboxmalloc Threads::Threads)

add_test(NAME 08_bench_pure COMMAND 08_bench_pure)
add_executable(10_bench_diag 10_bench_diag.c)
target_link_libraries(10_bench_diag slotsboxmalloc Threads::Threads)

add_executable(11_bench_shallow 11_bench_shallow.c)
target_link_libraries(11_bench_shallow slotsboxmalloc Threads::Threads)

add_executable(12_bench_lock 12_bench_lock.c)
target_link_libraries(12_bench_lock Threads::Threads)

add_executable(14_bench_alloc_only 14_bench_alloc_only.c)
target_link_libraries(14_bench_alloc_only slotsboxmalloc Threads::Threads)

add_executable(15_bench_dedicated 15_bench_dedicated.c)
target_link_libraries(15_bench_dedicated slotsboxmalloc Threads::Threads)

add_test(NAME 09_bench_sharded COMMAND 09_bench_sharded)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    foreach(t 01_basic 02_bench 03_stress 04_multithread 05_multiprocess 06_multiprocess_multithread 07_bench_concurrent 08_bench_pure)
        target_compile_definitions(${t} PRIVATE ENABLE_LOG)
    endforeach()
endif()
add_executable(16_bench_malloc 16_bench_malloc.c)
target_link_libraries(16_bench_malloc slotsboxmalloc Threads::Threads)

add_executable(17_data_integrity 17_data_integrity.c)
target_link_libraries(17_data_integrity slotsboxmalloc)
add_test(NAME 17_data_integrity COMMAND 17_data_integrity)

# -- 18: randomized model check (overlap / capacity / no false OOM) --
add_executable(18_model_check 18_model_check.c)
target_link_libraries(18_model_check slotsboxmalloc Threads::Threads)
add_test(NAME 18_model_check COMMAND 18_model_check 4 10000)
set_tests_properties(18_model_check PROPERTIES TIMEOUT 120)
