cmake_minimum_required(VERSION 3.10)
project(submarine_multistream)

set(CMAKE_CXX_STANDARD 14)

# Detect architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(ORBBEC_LIB_DIR "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7")
    set(ORBBEC_LIB_DIR "arm32")
else()
    set(ORBBEC_LIB_DIR "linux_x64")
endif()

# OrbbecSDK path
if(EXISTS "${CMAKE_SOURCE_DIR}/../OrbbecSDK")
    set(ORBBEC_SDK "${CMAKE_SOURCE_DIR}/../OrbbecSDK")
elseif(EXISTS "$ENV{HOME}/OrbbecSDK")
    set(ORBBEC_SDK "$ENV{HOME}/OrbbecSDK")
else()
    message(FATAL_ERROR "Cannot find OrbbecSDK")
endif()

# Find OpenCV
find_package(OpenCV REQUIRED)

include_directories(
    ${ORBBEC_SDK}/include
    ${ORBBEC_SDK}/examples/cpp
    ${OpenCV_INCLUDE_DIRS}
)

link_directories(
    ${ORBBEC_SDK}/lib/${ORBBEC_LIB_DIR}
)

# Find OpenGL and GLFW
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

# Original multistream viewer
add_executable(submarine_multistream submarine_multistream.cpp)
target_link_libraries(submarine_multistream
    OrbbecSDK glfw ${OPENGL_LIBRARIES} ${OpenCV_LIBS} pthread
)

# Camera sender (for submarine Pi with camera)
add_executable(camera_sender camera_sender.cpp)
target_link_libraries(camera_sender
    OrbbecSDK pthread
)

# Camera receiver (for processing Pi)
add_executable(camera_receiver camera_receiver.cpp)
target_link_libraries(camera_receiver
    OrbbecSDK glfw ${OPENGL_LIBRARIES} ${OpenCV_LIBS} pthread
)

# Set RPATH
set_target_properties(submarine_multistream camera_sender camera_receiver PROPERTIES
    BUILD_RPATH "${ORBBEC_SDK}/lib/${ORBBEC_LIB_DIR}"
    INSTALL_RPATH "${ORBBEC_SDK}/lib/${ORBBEC_LIB_DIR}"
)
