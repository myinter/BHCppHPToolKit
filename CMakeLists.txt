cmake_minimum_required(VERSION 3.20)

project(
    BHCppHPToolKit
    VERSION 0.1.0
    DESCRIPTION "A header-only high-performance C++ toolkit"
    LANGUAGES CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

add_library(BHCppHPToolKit INTERFACE)
add_library(BHCppHPToolKit::BHCppHPToolKit ALIAS BHCppHPToolKit)

target_compile_features(BHCppHPToolKit INTERFACE cxx_std_11)

target_include_directories(
    BHCppHPToolKit
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set(BHCPPHP_TOOLKIT_PUBLIC_HEADERS
    include/BHCppHPToolKit.hpp
)

set(BHCPPHP_TOOLKIT_SOURCE_HEADERS
    Foundations/MathBits.hpp
    HPContainer/PooledHashList.hpp
    HPContainer/PooledLinkedHashList.hpp
    HPContainer/PooledHashMap.hpp
    HPContainer/PooledList.hpp
    HPContainer/PooledMap.hpp
    HPContainer/PooledVector.hpp
    HPContainer/SegVector.hpp
    HPContainer/RWRingQueue.hpp
    Memory/BlockMemPool.hpp
    Memory/SegmentedObjectPool.hpp
    MultiPlatforms/PlatformMemory.hpp
    MultiPlatforms/PlatformThread.hpp
    MultiThreadAndMutex/BHSync.hpp
)

install(
    TARGETS BHCppHPToolKit
    EXPORT BHCppHPToolKitTargets
)

install(
    FILES ${BHCPPHP_TOOLKIT_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}
)

install(
    DIRECTORY Foundations HPContainer Memory MultiPlatforms MultiThreadAndMutex
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/BHCppHPToolKitConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BHCppHPToolKitConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BHCppHPToolKitConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BHCppHPToolKit"
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/BHCppHPToolKitConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/BHCppHPToolKitConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BHCppHPToolKit"
)

install(
    EXPORT BHCppHPToolKitTargets
    FILE BHCppHPToolKitTargets.cmake
    NAMESPACE BHCppHPToolKit::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BHCppHPToolKit"
)
