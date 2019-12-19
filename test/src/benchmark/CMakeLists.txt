FIND_PACKAGE(benchmark)
IF (NOT benchmark_FOUND)
	message(WARNING "Disabling benchmarks")
	return()
ENDIF()

include(../source_groups.cmake)

ADD_EXECUTABLE(fso_benchmark
    	vecmat_benchmark.cpp
		${source_files}
)

#TARGET_COMPILE_OPTIONS( fso_benchmark PUBLIC
#	"-march=native"
#	)

TARGET_LINK_LIBRARIES( fso_benchmark PRIVATE
		benchmark
		code
)
