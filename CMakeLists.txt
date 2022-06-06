zephyr_include_directories(include)

add_subdirectory(lib)
add_subdirectory(logging)
add_subdirectory(net)

add_subdirectory_ifdef(CONFIG_GOLIOTH_SAMPLES_COMMON samples/common)
