# Same as the community x64-linux-dynamic triplet, but glad generates the
# core profile instead of the default compatibility profile (Window.cpp
# requests a 4.1 core context).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_FIXUP_ELF_RPATH ON)

set(GLAD_PROFILE core)
