project(CameraCppV4L2v4)

cmake_minimum_required(VERSION 3.10)

find_package(OpenCV REQUIRED)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME} 
               capturev4l2.cpp
)

target_link_libraries(${PROJECT_NAME} ${OpenCV_LIBS})

