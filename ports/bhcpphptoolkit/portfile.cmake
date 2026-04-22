vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Bighiung/BHCppHPToolKit
    REF v0.1.0
    SHA512 REPLACE_WITH_ARCHIVE_SHA512
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/BHCppHPToolKit")

file(INSTALL
    "${SOURCE_PATH}/LICENSE"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")
