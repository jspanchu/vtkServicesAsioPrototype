cmake_minimum_required(VERSION 3.12)
project(AsioServices LANGUAGES CXX)
find_package(VTK REQUIRED
COMPONENTS
    CommonSystem
)

if (NOT VTK_FOUND)
  message(FATAL_ERROR "Unable to find VTK.")
endif()

include(FetchContent)
FetchContent_Declare(
  asio
  GIT_REPOSITORY "https://github.com/chriskohlhoff/asio.git"
  GIT_TAG 147f7225a96d45a2807a64e443177f621844e51c # asio-1-24-0
)
FetchContent_MakeAvailable(asio)
set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include")
set(RXCPP_DIR "$ENV{RXCPP_DIR}")

message(STATUS "rxcpp: ${RXCPP_DIR}")
message(STATUS "asio.hpp: ${ASIO_INCLUDE_DIR}/asio.hpp")

# Prevent a "command line is too long" failure in Windows.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE "ON" CACHE BOOL "Force Ninja to use response files.")
add_executable(main MACOSX_BUNDLE main.cxx )
# for rxcpp, c++17 may be needed.
target_compile_features(main PRIVATE cxx_std_17)
target_include_directories(main PRIVATE ${RXCPP_DIR}/Rx/v2/src ${ASIO_INCLUDE_DIR})
target_link_libraries(main PRIVATE ${VTK_LIBRARIES})
# vtk_module_autoinit is needed
vtk_module_autoinit(
    TARGETS main
    MODULES ${VTK_LIBRARIES}
)
