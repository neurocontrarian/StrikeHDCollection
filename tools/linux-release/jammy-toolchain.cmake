# Build against Ubuntu 22.04's C library (glibc 2.35), so the program runs on
# older distros too. build.sh fills the sysroot and passes its path.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSROOT ${DS_SYSROOT})
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
# Other headers (X11, Wayland, ALSA..., loaded by SDL at run time) come from
# the build machine, after the sysroot's.
set(CMAKE_C_FLAGS_INIT "-idirafter /usr/include")
set(CMAKE_CXX_FLAGS_INIT "-idirafter /usr/include")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
