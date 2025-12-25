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

# Find OpenGL, GLFW and GLEW (optional - only for programs that need them)
find_package(OpenGL)
find_package(glfw3)
find_package(GLEW)

# Original multistream viewer
add_executable(submarine_multistream submarine_multistream.cpp)
target_link_libraries(submarine_multistream
    OrbbecSDK glfw ${OPENGL_LIBRARIES} ${OpenCV_LIBS} pthread
)

# Submarine sender (with 3D mapping support)
add_executable(submarine_sender submarine_sender.cpp)
target_link_libraries(submarine_sender
    OrbbecSDK ${OpenCV_LIBS} pthread
)

# Submarine receiver
add_executable(submarine_receiver submarine_receiver.cpp)
target_link_libraries(submarine_receiver
    ${OpenCV_LIBS} pthread
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

# Professional 3D Point Cloud Viewer (with OpenCV Viz)
add_executable(submarine_3d_viz submarine_3d_viz.cpp)
target_link_libraries(submarine_3d_viz
    OrbbecSDK ${OpenCV_LIBS} pthread
)

# Professional 3D Point Cloud Viewer (OpenGL with grid and axes)
add_executable(submarine_3d_map submarine_3d_map.cpp)
target_link_libraries(submarine_3d_map
    OrbbecSDK glfw GLEW::GLEW ${OPENGL_LIBRARIES} ${OpenCV_LIBS} pthread
)

# GUI Launcher
add_executable(submarine_launcher submarine_launcher.cpp)
target_link_libraries(submarine_launcher ${OpenCV_LIBS})

# TCP Sender (reliable streaming)
add_executable(submarine_sender_tcp submarine_sender_tcp.cpp)
target_link_libraries(submarine_sender_tcp OrbbecSDK ${OpenCV_LIBS} pthread)

# TCP Receiver (reliable streaming)
add_executable(submarine_receiver_tcp submarine_receiver_tcp.cpp)
target_link_libraries(submarine_receiver_tcp ${OpenCV_LIBS} pthread)

# Depth+3D Sender (Depth + 2D Map + 3D Point Cloud)
add_executable(submarine_sender_depth3d submarine_sender_depth3d.cpp)
target_link_libraries(submarine_sender_depth3d OrbbecSDK ${OpenCV_LIBS} pthread)

# Depth+3D Receiver (Depth + 2D Map + 3D Point Cloud)
add_executable(submarine_receiver_depth3d submarine_receiver_depth3d.cpp)
target_link_libraries(submarine_receiver_depth3d ${OpenCV_LIBS} pthread)

# RAW Sender (sends raw depth, receiver processes)
add_executable(submarine_sender_raw submarine_sender_raw.cpp)
target_link_libraries(submarine_sender_raw OrbbecSDK ${OpenCV_LIBS} pthread)

# RAW Receiver (receives raw depth, does all processing)
add_executable(submarine_receiver_raw submarine_receiver_raw.cpp)
target_link_libraries(submarine_receiver_raw ${OpenCV_LIBS} pthread)

# Individual stream senders
add_executable(color_sender color_sender.cpp)
target_link_libraries(color_sender OrbbecSDK ${OpenCV_LIBS} pthread)

add_executable(depth_sender depth_sender.cpp)
target_link_libraries(depth_sender OrbbecSDK ${OpenCV_LIBS} pthread)

add_executable(map_sender map_sender.cpp)
target_link_libraries(map_sender OrbbecSDK ${OpenCV_LIBS} pthread)

add_executable(depth_3d_sender depth_3d_sender.cpp)
target_link_libraries(depth_3d_sender OrbbecSDK ${OpenCV_LIBS} pthread)

# Individual stream receivers
add_executable(color_receiver color_receiver.cpp)
target_link_libraries(color_receiver ${OpenCV_LIBS} pthread)

add_executable(depth_receiver depth_receiver.cpp)
target_link_libraries(depth_receiver ${OpenCV_LIBS} pthread)

add_executable(map_receiver map_receiver.cpp)
target_link_libraries(map_receiver ${OpenCV_LIBS} pthread)

add_executable(depth_3d_receiver depth_3d_receiver.cpp)
target_link_libraries(depth_3d_receiver ${OpenCV_LIBS} pthread)

# Switchable stream sender/receiver (NEW - on-demand streaming)
add_executable(switchable_sender switchable_sender.cpp)
target_link_libraries(switchable_sender OrbbecSDK ${OpenCV_LIBS} pthread)

add_executable(switchable_receiver switchable_receiver.cpp)
target_link_libraries(switchable_receiver ${OpenCV_LIBS} pthread)

# UDP Switchable stream sender/receiver (UDP-based, no connection issues)
add_executable(udp_switchable_sender udp_switchable_sender.cpp)
target_link_libraries(udp_switchable_sender OrbbecSDK ${OpenCV_LIBS} pthread)

add_executable(udp_switchable_receiver udp_switchable_receiver.cpp)
target_link_libraries(udp_switchable_receiver ${OpenCV_LIBS} pthread)

# Submarine Vision Sender (Depth + 2D Map + 3D Depth for point cloud)
add_executable(submarine_vision_sender submarine_vision_sender.cpp)
target_link_libraries(submarine_vision_sender OrbbecSDK ${OpenCV_LIBS} pthread)

# Submarine Vision Receiver (Depth + 2D Map + 3D Point Cloud visualization)
add_executable(submarine_vision_receiver submarine_vision_receiver.cpp)
target_link_libraries(submarine_vision_receiver ${OpenCV_LIBS} pthread)

# Set RPATH
set_target_properties(submarine_multistream submarine_sender submarine_receiver camera_sender camera_receiver submarine_3d_viz submarine_3d_map submarine_sender_tcp submarine_receiver_tcp color_sender depth_sender map_sender depth_3d_sender color_receiver depth_receiver map_receiver depth_3d_receiver switchable_sender switchable_receiver udp_switchable_sender udp_switchable_receiver submarine_vision_sender submarine_vision_receiver PROPERTIES
    BUILD_RPATH "${ORBBEC_SDK}/lib/${ORBBEC_LIB_DIR}"
    INSTALL_RPATH "${ORBBEC_SDK}/lib/${ORBBEC_LIB_DIR}"
)
