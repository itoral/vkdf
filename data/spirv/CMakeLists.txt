include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/build-shaders.cmake)

SET(EXAMPLE_NAME common_shader)

build_shaders()

add_custom_target(${EXAMPLE_NAME} ALL DEPENDS ${SHADER_OUTPUTS} ${GEN_SHADER_OUTPUTS})
