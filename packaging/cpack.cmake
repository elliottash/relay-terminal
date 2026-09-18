# SPDX-License-Identifier: GPL-3.0-or-later
# CPack configuration for Relay's binary packages (.deb today).
#
# One .deb is built per target distribution inside that distribution's container, so the Qt ABI
# it links matches that distribution's:
#
#   cmake -S . -B build-deb -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
#         -DBUILD_TESTING=OFF -DRELAY_VERSION_SUFFIX=~beta.1
#   cmake --build build-deb && (cd build-deb && cpack -G DEB)
#
# packaging/deb/build-deb.sh wraps this. Library dependencies (Qt, SyntaxHighlighting, QtPdf)
# are computed by dpkg-shlibdeps; the runtime-only ones (Python, Bash) are listed here.

set(RELAY_VERSION_SUFFIX "" CACHE STRING "Pre-release suffix appended to the package version, e.g. ~beta.1")
set(RELAY_PACKAGE_DISTRO "" CACHE STRING "Distribution tag for package file names, e.g. ubuntu24.04; detected when empty")
set(RELAY_PACKAGE_REVISION "1" CACHE STRING "Debian package revision")

if(NOT RELAY_PACKAGE_DISTRO AND EXISTS /etc/os-release)
    file(STRINGS /etc/os-release _relay_os_id REGEX "^ID=")
    file(STRINGS /etc/os-release _relay_os_ver REGEX "^VERSION_ID=")
    string(REGEX REPLACE "^ID=\"?([^\"]*)\"?$" "\\1" _relay_os_id "${_relay_os_id}")
    string(REGEX REPLACE "^VERSION_ID=\"?([^\"]*)\"?$" "\\1" _relay_os_ver "${_relay_os_ver}")
    set(RELAY_PACKAGE_DISTRO "${_relay_os_id}${_relay_os_ver}")
endif()

set(CPACK_PACKAGE_NAME "relay")
set(CPACK_PACKAGE_VENDOR "Relay contributors")
set(CPACK_PACKAGE_CONTACT "Elliott Ash <e@elliottash.com>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}${RELAY_VERSION_SUFFIX}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Linux terminal with a rich prompt and bring-your-own-key agents")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/elliottash/relay-terminal")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_PACKAGE_NAME "relay")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${RELAY_PACKAGE_REVISION}")
if(RELAY_PACKAGE_DISTRO)
    string(APPEND CPACK_DEBIAN_PACKAGE_RELEASE "~${RELAY_PACKAGE_DISTRO}")
endif()
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "x11")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Linux terminal with a rich prompt and bring-your-own-key agents
 Relay has its own terminal engine and a real text editor for composing shell
 commands and agent requests, plus agents that use your own API key for
 OpenAI-compatible providers. No accounts, no telemetry.")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# CPack appends the dpkg-shlibdeps result to this list.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "python3 (>= 3.10), bash")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libsecret-tools, xdg-utils")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "gnome-keyring | kwalletmanager")

set(CPACK_GENERATOR "DEB")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES "/\\\\.git/" "/build.*/" "/tmp/" "__pycache__" "\\\\.swp$" "/\\\\.claude/")
include(CPack)
