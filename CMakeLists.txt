CMAKE_MINIMUM_REQUIRED(VERSION 3.9)

PROJECT(vkdf VERSION 0.1.0 DESCRIPTION "vkdf")

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
SET(CMAKE_CXX_FLAGS "-W -Wall -Wno-extra")
SET(CMAKE_EXE_LINKER_FLAGS "-lm")

IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE Debug)
ENDIF()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	ADD_DEFINITIONS (-DENABLE_DEBUG)
	OPTION(ENABLE_VALIDATION "enable vulkan validation layer" ON)
ELSE()
	UNSET(ENABLE_VALIDATION CACHE)
ENDIF()

OPTION(VKDF_BUILD_TESTS "build tests" ON)

set(VULKAN_SDK "$ENV{VULKAN_SDK}" CACHE STRING "LunarG Vulkan SDK path")
if (VULKAN_SDK)
		set(ENV{VULKAN_SDK} ${VULKAN_SDK})
		SET(ENV{VK_LAYER_PATH} "${VULKAN_SDK}/etc/explicit_layer.d")
endif ()

#add_subdirectory (vkh)

IF (ENABLE_VALIDATION)
	ADD_DEFINITIONS (-DENABLE_VALIDATION=1)
ELSE()
	ADD_DEFINITIONS (-DENABLE_VALIDATION=0)
ENDIF ()

ADD_DEFINITIONS (-DVKDF_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}/data/")

#be aware that system libraries have priority on SDK in the finding.
FIND_PACKAGE(Vulkan REQUIRED)
FIND_PACKAGE(GLFW3)
FIND_PACKAGE(ASSIMP REQUIRED)
FIND_PACKAGE(GLIB2 REQUIRED)
FIND_PACKAGE(GLM REQUIRED)

INCLUDE(FindPkgConfig)

PKG_SEARCH_MODULE(SDL2 REQUIRED sdl2)
PKG_SEARCH_MODULE(SDL2IMAGE REQUIRED SDL2_image>=2.0.0)

# Find glslc shader compiler.
# On Android, the NDK includes the binary, so no external dependency.
if(ANDROID)
	file(GLOB glslc-folders ${ANDROID_NDK}/shader-tools/*)
else()
	file(GLOB glslc-folders ${VULKAN_SDK}/bin)
endif()
FIND_PROGRAM(GLSLC glslc HINTS ${glslc-folders})


SET(VKDF_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/framework")
SET(VKDF_FIX_GLSL_PY "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fixup-glsl.py")

FILE(GLOB VKDF_SRC framework/*.cpp)

ADD_DEFINITIONS (-DVKDF_PLATFORM_SDL=1)
list(REMOVE_ITEM VKDF_SRC "${CMAKE_CURRENT_SOURCE_DIR}/framework/vkdf-platform-glfw.cpp" )

#common shaders
add_subdirectory(data/spirv)

ADD_LIBRARY(${PROJECT_NAME} SHARED ${VKDF_SRC})

add_dependencies(${PROJECT_NAME} common_shader)

TARGET_INCLUDE_DIRECTORIES(vkdf PRIVATE
	${Vulkan_INCLUDE_DIRS}
	${SDL2_INCLUDE_DIRS}
	${SDL2IMAGE_INCLUDE_DIRS}
	${ASSIMP_INCLUDE_DIRS}
	${GLIB2_INCLUDE_DIR}
	${GLM_INCLUDE_DIRS}
	${GLFW3_INCLUDE_DIR}
	${VKDF_INCLUDE_DIR}
)

TARGET_LINK_LIBRARIES(vkdf
	${Vulkan_LIBRARIES}
	${SDL2_LIBRARIES}
	${SDL2IMAGE_LIBRARIES}
	${ASSIMP_LIBRARIES}
	${GLIB2_LIBRARIES}
	${GLFW3_LIBRARY}
)

add_subdirectory(demos/triangle)
add_subdirectory(demos/offscreen)
add_subdirectory(demos/mesh)
add_subdirectory(demos/depth)
add_subdirectory(demos/model)
add_subdirectory(demos/texture)
add_subdirectory(demos/shadow)
add_subdirectory(demos/scene)
add_subdirectory(demos/scenelight)
add_subdirectory(demos/pbr)
add_subdirectory(demos/sponza)

#common models
file(GLOB_RECURSE DATAS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "data/models/*")
FOREACH(DATA_FILE ${DATAS})
	GET_FILENAME_COMPONENT(copy-dest-dir ${CMAKE_CURRENT_BINARY_DIR}/${DATA_FILE} DIRECTORY)
	SET(copy-output ${CMAKE_CURRENT_BINARY_DIR}/${DATA_FILE})
	ADD_CUSTOM_COMMAND(
	   OUTPUT  ${copy-output}
	   COMMAND ${CMAKE_COMMAND} -E make_directory ${copy-dest-dir}
	   COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${DATA_FILE}" "${copy-output}"
	   COMMENT "Copying ${DATA_FILE} to ${copy-output}"
	   DEPENDS ${DATA_FILE}
	   VERBATIM
	)
ENDFOREACH()
add_custom_target(common_models_copy ALL DEPENDS ${DATAS})
add_dependencies(vkdf common_models_copy)
