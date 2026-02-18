set(src_dirs
    audio_pipeline
    speech_recognition
    command_handler
    audio_player
)

set(include_dirs
    audio_pipeline
    speech_recognition
    command_handler
    audio_player
)

set(requires
    BSP
    NET
    esp-sr
)

idf_component_register(
    SRC_DIRS      ${src_dirs}
    INCLUDE_DIRS  ${include_dirs}
    REQUIRES      ${requires}
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -ffast-math
    -O3
)
