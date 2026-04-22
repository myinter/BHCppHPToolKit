cmake_minimum_required(VERSION 3.20)

project(BHCppHPToolKitFindPackageExample LANGUAGES CXX)

find_package(BHCppHPToolKit CONFIG REQUIRED)

add_executable(bhcpphptoolkit_example main.cpp)
target_link_libraries(bhcpphptoolkit_example PRIVATE BHCppHPToolKit::BHCppHPToolKit)
