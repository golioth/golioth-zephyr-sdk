zephyr_library_sources_ifdef(CONFIG_GOLIOTH coap_utils.c golioth.c)
zephyr_library_sources_ifdef(CONFIG_GOLIOTH_FW fw.c)
zephyr_library_sources_ifdef(CONFIG_GOLIOTH_RPC rpc.c)
zephyr_library_sources_ifdef(CONFIG_GOLIOTH_SETTINGS settings.c)
zephyr_library_sources_ifdef(CONFIG_GOLIOTH_SYSTEM_CLIENT system_client.c)

if(CONFIG_GOLIOTH_AUTH_METHOD_CERT)
  foreach(type IN ITEMS ca crt key)
    string(TOUPPER ${type} type_upper)
    set(path ${CONFIG_GOLIOTH_SYSTEM_CLIENT_${type_upper}_PATH})

    if(NOT IS_ABSOLUTE ${path})
      set(path ${APPLICATION_SOURCE_DIR}/${path})
    endif()

    generate_inc_file_for_target(app ${path}
      ${ZEPHYR_BINARY_DIR}/include/generated/golioth-systemclient-${type}.inc)
  endforeach()
endif()
