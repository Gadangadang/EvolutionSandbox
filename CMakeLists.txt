cmake_minimum_required(VERSION 3.20)
project(evolution_sandbox C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# raylib (windowing, drawing, input) is fetched at a pinned release; nothing is installed system-wide.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(evolution_sandbox
  src/main.cpp
  src/world.cpp
  src/brain.cpp
  src/climate.cpp
  src/parallel.cpp
  src/render.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(evolution_sandbox PRIVATE raylib Threads::Threads)
target_compile_options(evolution_sandbox PRIVATE -Wall -Wextra)

# Optimisation. Release already means -O3. On top of that:
#  - tune for this machine's CPU (turn EVO_NATIVE off to build a binary for other machines),
#  - link-time optimisation across our files,
#  - -fno-math-errno: maths functions need not set errno, so they can be inlined and vectorised.
# Deliberately not -ffast-math: it measured no faster here, and it lets the compiler assume
# there are no NaNs or infinities and reorder sums, which makes bugs and results harder to trust.
option(EVO_NATIVE "Tune for the CPU of the building machine" ON)
include(CheckCXXCompilerFlag)
if(EVO_NATIVE)
  check_cxx_compiler_flag(-mcpu=native HAS_MCPU_NATIVE)
  check_cxx_compiler_flag(-march=native HAS_MARCH_NATIVE)
  if(HAS_MCPU_NATIVE)
    target_compile_options(evolution_sandbox PRIVATE -mcpu=native)
  elseif(HAS_MARCH_NATIVE)
    target_compile_options(evolution_sandbox PRIVATE -march=native)
  endif()
endif()
target_compile_options(evolution_sandbox PRIVATE $<$<NOT:$<CONFIG:Debug>>:-fno-math-errno>)
include(CheckIPOSupported)
check_ipo_supported(RESULT HAS_IPO OUTPUT IPO_ERROR)
if(HAS_IPO)
  set_property(TARGET evolution_sandbox PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  set_property(TARGET evolution_sandbox PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()
