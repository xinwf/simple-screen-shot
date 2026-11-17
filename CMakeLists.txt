cmake_minimum_required(VERSION 3.16)
project(SimpleScreenShot VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo pangocairo fontconfig)
pkg_check_modules(X11_DEPS REQUIRED IMPORTED_TARGET x11 xext xtst xfixes)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

# Third-party C libraries compiled fully as C++20.
file(GLOB_RECURSE THIRD_PARTY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/quirc/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgif/*.cpp"
)
set_source_files_properties(${THIRD_PARTY_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "QUIRC_MAX_REGIONS=65534"
    COMPILE_OPTIONS "-Wno-sign-compare;-Wno-missing-field-initializers;-Wno-maybe-uninitialized;-Wno-unused-variable"
)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

if(NOT APP_SOURCES)
    message(FATAL_ERROR "No application sources found")
endif()

add_executable(screencapture
    ${THIRD_PARTY_SOURCES}
    ${APP_SOURCES}
)

target_include_directories(screencapture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quirc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgif
)

target_compile_options(screencapture PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

target_compile_definitions(screencapture PRIVATE SC_APP_VERSION="${PROJECT_VERSION}")

target_link_options(screencapture PRIVATE
    -static-libgcc
    -static-libstdc++
)

target_link_libraries(screencapture PRIVATE
    PkgConfig::GTK3
    PkgConfig::CAIRO
    PkgConfig::X11_DEPS
    PkgConfig::GIO
    pthread
)

# Runtime resources: copy assets next to the executable output.
add_custom_command(TARGET screencapture POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:screencapture>/assets
    COMMENT "Syncing assets to build output"
)

# ---------------------------------------------------------------------------
# Debian (.deb) packaging via CPack. Only used by `make install` / `cpack`;
# the normal development build above is completely unaffected.
#
# Usage:
#   cmake -S . -B build && cmake --build build
#   cpack -G DEB --config build/CPackConfig.cmake
# (or simply: cd build && make package)
#
# Install layout (FHS):
#   /usr/bin/screencapture
#   /usr/share/simplescreenshot/assets/...
#   /usr/share/applications/simplescreenshot.desktop
#   /usr/share/pixmaps/simplescreenshot.png
# ---------------------------------------------------------------------------
set(APP_ID "simplescreenshot")

include(GNUInstallDirs)
install(TARGETS screencapture RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${APP_ID}/assets)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/${APP_ID}.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pixmaps
        RENAME ${APP_ID}.png)

# Menu icon: the original logo.png. It is installed as
# /usr/share/pixmaps/simplescreenshot.png (above) and GTK's legacy pixmaps
# fallback resolves the .desktop Icon=simplescreenshot to it.

# Debian policy doc files: copyright, compressed changelog and man page.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/copyright
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${APP_ID})
find_program(GZIP_TOOL gzip)
if(GZIP_TOOL)
    execute_process(COMMAND ${GZIP_TOOL} -9n -c ${CMAKE_CURRENT_SOURCE_DIR}/packaging/changelog
                    OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/changelog.gz)
    execute_process(COMMAND ${GZIP_TOOL} -9n -c ${CMAKE_CURRENT_SOURCE_DIR}/packaging/screencapture.1
                    OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/screencapture.1.gz)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/changelog.gz
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${APP_ID}
            RENAME changelog.Debian.gz)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/screencapture.1.gz
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
else()
    message(WARNING "gzip not found: changelog/man page will be omitted from the deb")
endif()

set(CPACK_PACKAGE_NAME ${APP_ID})
set(CPACK_PACKAGE_VENDOR "xinwf")
set(CPACK_PACKAGE_CONTACT "xinwf <xinwf18@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Simple screenshot tool for X11 (GTK3)")
set(CPACK_PACKAGE_DESCRIPTION
    "Lightweight screenshot utility for X11 desktops: region capture with
  annotation (arrow, rect, ellipse, line, text, mosaic, numbering),
  pinned screenshots, long screenshot stitching, QR code scanning,
  OCR text recognition and screen recording.")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
# Debian package revision: bump whenever the packaged content changes
# (features, icons, docs, install layout) so apt treats it as an upgrade of
# the same upstream version instead of skipping an identical 1.0.0.
set(CPACK_DEBIAN_PACKAGE_RELEASE "2")
set(CPACK_STRIP_FILES ON)
# Debian-standard directory permissions (lintian: non-standard-dir-perm).
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
# Exact per-library Depends computed from the packaged binary (dpkg-shlibdeps).
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Optional runtime helpers, all shipped as Recommends (not hard Depends):
# OCR shells out to tesseract (chi_sim+eng), screen recording shells out to
# ffmpeg, and xclip persists clipboard images without a clipboard manager.
# Each feature checks its helper at use time and shows a "missing
# dependency" dialog instead of failing silently. QR decoding needs no
# external package (quirc is vendored in third_party/).
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
    "ffmpeg, xclip, tesseract-ocr, tesseract-ocr-chi-sim")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_GENERATOR "DEB")

include(CPack)
