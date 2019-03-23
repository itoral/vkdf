include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/build-shaders.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/copy-data.cmake)

SET(EXAMPLE_NAME pbr)

build_shaders()
copy_data(data/*)

add_executable(${EXAMPLE_NAME} main.cpp ${SHADER_OUTPUTS} ${GEN_SHADER_OUTPUTS})

TARGET_INCLUDE_DIRECTORIES(${EXAMPLE_NAME} PRIVATE
	${GLIB2_INCLUDE_DIR}
	${VKDF_INCLUDE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/external
)

TARGET_LINK_LIBRARIES(${EXAMPLE_NAME}
	vkdf
)

add_dependencies("${EXAMPLE_NAME}" "${EXAMPLE_NAME}_DataCopy")
