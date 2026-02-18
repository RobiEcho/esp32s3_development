set(src_dirs
    video_decode
)

set(include_dirs
    video_decode
)

set(requires
    lib
)

idf_component_register(
    SRC_DIRS      ${src_dirs}
    INCLUDE_DIRS  ${include_dirs}
    REQUIRES      ${requires}
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -ffast-math
    -O3
    -Wno-error=format
    -Wno-format
)
