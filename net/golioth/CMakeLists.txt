zephyr_library_sources(
  coap_req.c
  coap_utils.c
  golioth.c
  golioth_utils.c
  lightdb.c
  stream.c
)
zephyr_library_sources_ifdef(CONFIG_GOLIOTH_FW fw.c)
zephyr_library_sources_ifdef(CONFIG_GOLIOTH_RPC rpc.c)
zephyr_library_sources_ifdef(CONFIG_GOLIOTH_SETTINGS settings.c)
zephyr_library_sources_ifdef(CONFIG_GOLIOTH_SYSTEM_CLIENT system_client.c)

if(CONFIG_GOLIOTH_AUTH_METHOD_CERT)
  foreach(type IN ITEMS ca crt key)
    string(TOUPPER ${type} type_upper)
    set(path ${CONFIG_GOLIOTH_SYSTEM_CLIENT_${type_upper}_PATH})

    if(NOT IS_ABSOLUTE ${path})
      if(EXISTS ${APPLICATION_SOURCE_DIR}/${path})
        set(path ${APPLICATION_SOURCE_DIR}/${path})
      elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${path})
        set(path ${CMAKE_CURRENT_SOURCE_DIR}/${path})
      else()
        message(FATAL_ERROR "${path} configured by CONFIG_GOLIOTH_SYSTEM_CLIENT_${type_upper}_PATH does not exist")
      endif()
    endif()

    generate_inc_file_for_target(app ${path}
      ${ZEPHYR_BINARY_DIR}/include/generated/golioth-systemclient-${type}.inc)
  endforeach()
endif()

if(CONFIG_GOLIOTH_CIPHERSUITES)
  string(REGEX MATCHALL "[^,; ]+" ciphersuites ${CONFIG_GOLIOTH_CIPHERSUITES})
  string(JOIN ", \\\n\t" ciphersuites_macro_value ${ciphersuites})

  set(ciphersuites_generated "#define GOLIOTH_CIPHERSUITES \\\n\t${ciphersuites_macro_value}\n")
else()
  set(ciphersuites_generated "#define GOLIOTH_CIPHERSUITES\n")
endif()

file(WRITE
  ${ZEPHYR_BINARY_DIR}/include/generated/golioth_ciphersuites.h
  "${ciphersuites_generated}")
