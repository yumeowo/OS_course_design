cmake_minimum_required(VERSION 3.10)

project(OS_course_design)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

aux_source_directory(src SOURCES)

add_executable(${PROJECT_NAME} ${SOURCES})

