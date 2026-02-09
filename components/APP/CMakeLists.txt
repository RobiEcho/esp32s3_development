set(src_dirs
    lvgl_ui
)

set(include_dirs
    lvgl_ui
)

set(requires
    lvgl
    LVGL_DRV
)

idf_component_register(
    SRC_DIRS      ${src_dirs}
    INCLUDE_DIRS  ${include_dirs}
    REQUIRES      ${requires}
)

