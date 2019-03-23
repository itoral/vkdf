include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/build-shaders.cmake)

SET(EXAMPLE_NAME sponza)

build_shaders()

add_executable(${EXAMPLE_NAME} main.cpp ${SHADER_OUTPUTS} ${GEN_SHADER_OUTPUTS})

TARGET_INCLUDE_DIRECTORIES(${EXAMPLE_NAME} PRIVATE
	${GLIB2_INCLUDE_DIR}
	${VKDF_INCLUDE_DIR}
)

TARGET_LINK_LIBRARIES(${EXAMPLE_NAME}
	vkdf
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/copy-data.cmake)
copy_data(data/*)
add_dependencies("${EXAMPLE_NAME}" "${EXAMPLE_NAME}_DataCopy")
