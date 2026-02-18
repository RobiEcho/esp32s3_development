set(src_dirs
    image_decode
)

set(include_dirs
    image_decode
)

set(requires
    esp_jpeg
    esp_timer
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

