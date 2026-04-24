cmake_minimum_required(VERSION 3.20)

project(BHCppHPToolKitVSCodeDebug LANGUAGES CXX)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_executable(
    bhcpphptoolkit_vscode_example
    "${REPO_ROOT}/examples/find_package/main.cpp"
)

target_compile_features(bhcpphptoolkit_vscode_example PRIVATE cxx_std_17)

target_include_directories(
    bhcpphptoolkit_vscode_example
    PRIVATE
        "${REPO_ROOT}"
        "${REPO_ROOT}/include"
)
