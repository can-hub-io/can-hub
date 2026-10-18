# ccache as the compiler launcher whenever it is installed. It keys on content,
# not timestamps, so a fresh checkout (CI) still hits.
find_program(CAN_HUB_CCACHE ccache)
if(CAN_HUB_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "${CAN_HUB_CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CAN_HUB_CCACHE}")
endif()
