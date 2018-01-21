#ifndef __VKDF_H__
#define __VKDF_H__

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <glib.h>

// GLFW3
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// GLM
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// ASSIMP
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// SDL2 Image
#include <SDL/SDL_image.h>

#define VKDF_LOG_FPS_ENABLE 1

#define PI ((float) M_PI)
#define DEG_TO_RAD(x) ((float)((x) * PI / 180.0f))
#define RAD_TO_DEG(x) ((float)((x) * 180.0f / PI))

#define RAND_NEG(n) (random() % (2*n+1) - (2*n+1) / 2)
#define RAND(n) (random() % (n+1))

typedef struct {
   VkImage image;
   VkImageView view;
} VkdfSwapChainImage;


struct _VkdfContext;

typedef void (*VkdfRebuildSwapChainCB)(struct _VkdfContext *ctx,
                                       void *user_data);

struct _VkdfContext {
   // Vulkan instance
   VkInstance inst;
   uint32_t inst_extension_count;
   char **inst_extensions;

   // Debug callback
   VkDebugReportCallbackEXT debug_callback;

   // Vulkan device
   uint32_t phy_device_count;
   VkPhysicalDevice *phy_devices;
   VkPhysicalDevice phy_device;
   VkPhysicalDeviceProperties phy_device_props;
   VkPhysicalDeviceMemoryProperties phy_device_mem_props;
   uint32_t queue_count;
   VkQueueFamilyProperties *queues;
   int32_t gfx_queue_index;
   int32_t pst_queue_index;
   VkQueue gfx_queue;
   VkQueue pst_queue;
   VkDevice device;
   uint32_t device_extension_count;
   const char **device_extensions;

   // Window and surface
   GLFWwindow *window;
   VkSurfaceKHR surface;
   VkSurfaceCapabilitiesKHR surface_caps;
   VkFormat surface_format;
   uint32_t width;
   uint32_t height;

   // Swap chain
   VkSwapchainKHR swap_chain;
   uint32_t swap_chain_length;
   VkdfSwapChainImage *swap_chain_images;
   VkSemaphore *acquired_sem;
   VkSemaphore *draw_sem;
   uint32_t swap_chain_index;

   // Swap chain rebuild callbacks
   VkdfRebuildSwapChainCB before_rebuild_swap_chain_cb;
   VkdfRebuildSwapChainCB after_rebuild_swap_chain_cb;
   void *rebuild_swap_chain_cb_data;
};

typedef struct _VkdfContext VkdfContext;

#include "vkdf-plane.hpp"
#include "vkdf-box.hpp"
#include "vkdf-util.hpp"
#include "vkdf-frustum.hpp"
#include "vkdf-thread-pool.hpp"
#include "vkdf-error.hpp"
#include "vkdf-init.hpp"
#include "vkdf-event-loop.hpp"
#include "vkdf-cmd-buffer.hpp"
#include "vkdf-buffer.hpp"
#include "vkdf-memory.hpp"
#include "vkdf-shader.hpp"
#include "vkdf-pipeline.hpp"
#include "vkdf-image.hpp"
#include "vkdf-sampler.hpp"
#include "vkdf-framebuffer.hpp"
#include "vkdf-renderpass.hpp"
#include "vkdf-descriptor.hpp"
#include "vkdf-barrier.hpp"
#include "vkdf-semaphore.hpp"
#include "vkdf-mesh.hpp"
#include "vkdf-model.hpp"
#include "vkdf-object.hpp"
#include "vkdf-light.hpp"
#include "vkdf-camera.hpp"
#include "vkdf-scene.hpp"

#endif
