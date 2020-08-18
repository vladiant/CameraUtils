project(CameraCppV4L2v1)

cmake_minimum_required(VERSION 3.10)

add_executable(${PROJECT_NAME} 
               capture_raw_frames.c
)

