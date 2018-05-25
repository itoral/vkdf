#ifndef __VKDF_DEPS_H__
#define __VKDF_DEPS_H__

#include "config.h"

// LIBC
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>

// LIBSTDC++
#include <vector>

// GLIB
#include <glib.h>

// Vulkan
#ifdef VKDF_PLATFORM_SDL
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#include <vulkan/vulkan.h>

// GLFW3
#ifdef VKDF_PLATFORM_GLFW
#include <GLFW/glfw3.h>
#endif

// SDL2
#ifdef VKDF_PLATFORM_SDL
#include <SDL2/SDL.h>
#endif

// SDL2 Image
#include <SDL2/SDL_image.h>

// GLM
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// ASSIMP
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#endif
