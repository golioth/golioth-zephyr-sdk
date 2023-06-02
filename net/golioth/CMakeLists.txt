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
zephyr_library_sources_ifdef(CONFIG_ZCBOR zcbor_utils.c)

if(CONFIG_GOLIOTH_AUTH_METHOD_CERT)
  set(path ${CONFIG_GOLIOTH_SYSTEM_CLIENT_CA_PATH})

  if(NOT IS_ABSOLUTE ${path})
    if(EXISTS ${APPLICATION_SOURCE_DIR}/${path})
      set(path ${APPLICATION_SOURCE_DIR}/${path})
    elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${path})
      set(path ${CMAKE_CURRENT_SOURCE_DIR}/${path})
    else()
      message(FATAL_ERROR "${path} configured by CONFIG_GOLIOTH_SYSTEM_CLIENT_CA_PATH does not exist")
    endif()
  endif()

  generate_inc_file_for_target(app ${path}
    ${ZEPHYR_BINARY_DIR}/include/generated/golioth-systemclient-ca.inc)
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

if(CONFIG_ZCBOR)
  file(READ ${ZEPHYR_ZCBOR_MODULE_DIR}/zcbor/VERSION zcbor_version)
  string(STRIP ${zcbor_version} zcbor_version)
  set(zcbor_version_regex "^([0-9]+)\.([0-9]+)\.([0-9]+)$")
  string(REGEX REPLACE ${zcbor_version_regex} "\\1" zcbor_version_major "${zcbor_version}")
  string(REGEX REPLACE ${zcbor_version_regex} "\\2" zcbor_version_minor "${zcbor_version}")
  zephyr_library_compile_definitions(ZCBOR_VERSION_MAJOR=${zcbor_version_major})
  zephyr_library_compile_definitions(ZCBOR_VERSION_MINOR=${zcbor_version_minor})
endif()
