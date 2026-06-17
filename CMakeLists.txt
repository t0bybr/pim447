if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library()

  zephyr_include_directories(include)
  # The driver library needs ZMK headers (e.g. zmk/events/...) which live in
  # the ZMK app's include dir. That dir is only added to the `app` target by
  # ZMK, not to module zephyr_libraries, so expose it explicitly here.
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)

  zephyr_library_sources_ifdef(CONFIG_ZMK_PIMORONI_PIM447 src/pimoroni_pim447.c src/pimoroni_pim447_led.c)
  target_sources_ifdef(CONFIG_ZMK_PIM447_BEHAVIORS app PRIVATE src/behaviors/behavior_pim447.c)

endif()