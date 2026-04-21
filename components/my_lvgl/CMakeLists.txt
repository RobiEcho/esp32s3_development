set(src_dirs
    driver_cfg/lvgl_port
    ui
)

set(include_dirs
    driver_cfg/lvgl_port
    ui
)

set(requires
    lvgl__lvgl
    BSP
)

idf_component_register(
    SRC_DIRS      ${src_dirs}
    INCLUDE_DIRS  ${include_dirs}
    REQUIRES      ${requires}
)
