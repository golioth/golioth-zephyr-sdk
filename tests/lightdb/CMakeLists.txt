# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(SNIPPET golioth-sample)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lightdb)

target_sources(app PRIVATE src/main.c)

function(resource_to_c target source_file generated_file content_type)
  add_custom_command(
    OUTPUT ${generated_file}
    COMMAND
    ${PYTHON_EXECUTABLE}
    ${APPLICATION_SOURCE_DIR}/scripts/resource_to_c.py
    -i ${source_file}
    -o ${generated_file}
    -t ${content_type}
    DEPENDS
    ${source_file}
    ${APPLICATION_SOURCE_DIR}/scripts/resource_to_c.py
  )

  # But first create a unique name for the custom target
  generate_unique_target_name_from_filename(${generated_file} generated_target_name)

  add_custom_target(${generated_target_name} DEPENDS ${generated_file})

  # Ensure 'generated_file' is generated before 'target' by creating a
  # dependency between the two targets
  add_dependencies(${target} ${generated_target_name})
endfunction()

foreach(name IN ITEMS small big-1024 big-2048)
  foreach(content_type IN ITEMS cbor json)
    resource_to_c(app
      ${APPLICATION_SOURCE_DIR}/resources/get/${name}.json
      ${ZEPHYR_BINARY_DIR}/include/generated/get-${name}-${content_type}.inc
      ${content_type})
  endforeach()
endforeach()
