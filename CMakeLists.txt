if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library()

  zephyr_include_directories(include)
  zephyr_library_sources_ifdef(CONFIG_ZMK_PIMORONI_PIM447 src/pimoroni_pim447.c src/pimoroni_pim447_led.c)
  target_sources_ifdef(CONFIG_ZMK_PIM447_BEHAVIORS app PRIVATE src/behaviors/behavior_pim447.c)

endif()