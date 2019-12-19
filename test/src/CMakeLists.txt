
include(source_groups.cmake)

add_executable(unittests main.cpp ${source_files})
target_link_libraries(unittests PRIVATE gtest)
target_link_libraries(unittests PRIVATE code)

target_include_directories(unittests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(unittests PROPERTIES FOLDER "tests")

file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../test_data" TEST_DATA_PATH)
string(REPLACE "\\" "\\\\" TEST_DATA_PATH "${TEST_DATA_PATH}")
target_compile_definitions(unittests PRIVATE "TEST_DATA_PATH=\"${TEST_DATA_PATH}\"")

INCLUDE(util)
COPY_FILES_TO_TARGET(unittests)

FIND_PACKAGE(benchmark)
IF (NOT benchmark_FOUND)
    message(WARNING "Disabling benchmarks")
    return()
ENDIF()

ADD_EXECUTABLE(fso_benchmark
        benchmark/vecmat_benchmark.cpp
        test_stubs.cpp
        )

#TARGET_COMPILE_OPTIONS( flexcore_benchmark PUBLIC
#	"-march=native"
#	)

TARGET_LINK_LIBRARIES( fso_benchmark PRIVATE
        benchmark
        code
        )
