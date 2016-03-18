set(RAJA_COMPILER "RAJA_COMPILER_GNU" CACHE STRING "")

set(CMAKE_C_COMPILER "/usr/apps/gnu/4.9.3/bin/gcc" CACHE PATH "")
set(CMAKE_CXX_COMPILER "/usr/apps/gnu/4.9.3/bin/g++" CACHE PATH "")

if(CMAKE_BUILD_TYPE MATCHES Release)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -mavx -finline-functions -finline-limit=20000 -std=c++0x" CACHE STRING "")
  set(RAJA_NVCC_FLAGS -O2; -restrict; -arch compute_35; -std c++11; --expt-extended-lambda; -x cu; -ccbin; ${CMAKE_CXX_COMPILER} CACHE LIST "")
elseif(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -mavx -finline-functions -finline-limit=20000 -std=c++0x" CACHE STRING "")
  set(RAJA_NVCC_FLAGS -g; -G; -O2; -restrict; -arch compute_35; -std c++11; --expt-extended-lambda; -x cu; -ccbin ${CMAKE_CXX_COMPILER} CACHE LIST "")
elseif(CMAKE_BUILD_TYPE MATCHES Debug)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -fpermissive -std=c++0x" CACHE STRING "")
  set(RAJA_NVCC_FLAGS -g; -G; -O0; -restrict; -arch compute_35; -std c++11; --expt-extended-lambda; -x cu; -ccbin ${CMAKE_CXX_COMPILER} CACHE LIST "")
endif()


set(RAJA_USE_CUDA On CACHE BOOL "")
set(RAJA_USE_OPENMP On CACHE BOOL "")

set(RAJA_RANGE_ALIGN 4 CACHE INT "")
set(RAJA_RANGE_MIN_LENGTH 32 CACHE INT "")
set(RAJA_DATA_ALIGN 64 CACHE INT "")
set(RAJA_COHERENCE_BLOCK_SIZE 64 CACHE INT "")