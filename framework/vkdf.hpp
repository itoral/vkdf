#ifndef __VKDF_H__
#define __VKDF_H__

#include "vkdf-deps.hpp"

#define VKDF_LOG_FPS_ENABLE 1

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
   VkPhysicalDeviceFeatures phy_device_features;
   uint32_t queue_count;
   VkQueueFamilyProperties *queues;
   int32_t gfx_queue_index;
   int32_t pst_queue_index;
   VkQueue gfx_queue;
   VkQueue pst_queue;
   VkDevice device;

   /* Extensions and features */
   uint32_t phy_device_extension_count;             // Available extensions
   VkExtensionProperties *phy_device_extensions;
   union {                                          // Enabled extensions
      bool enabled[2];
      struct {
         bool KHR_swapchain;
         bool KHR_maintenance1;
      };
   } device_extensions;
   VkPhysicalDeviceFeatures device_features;        // Enabled features

   // Window and surface
   GLFWwindow *window;
   VkSurfaceKHR surface;
   VkSurfaceCapabilitiesKHR surface_caps;
   VkSurfaceFormatKHR surface_format;
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

   // Framerate target
   float fps_target;
   double frame_time_budget;
   bool fps_target_from_env;
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
#include "vkdf-ssao.hpp"
#include "vkdf-scene.hpp"

#endif
