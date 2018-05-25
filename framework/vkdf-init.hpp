#ifndef __VKDF_INIT_H__
#define __VKDF_INIT_H__

#include "vkdf-deps.hpp"
#include "vkdf-error.hpp"
#include "vkdf-platform.hpp"

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
   VkdfPlatform platform;
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

void
vkdf_init(VkdfContext *ctx,
          uint32_t widht,
          uint32_t height,
          bool fullscreen,
          bool resizable,
          bool enable_validation);

inline void
vkdf_set_framerate_target(VkdfContext *ctx, float target)
{
   assert(target > 0.0f);

   if (!ctx->fps_target_from_env) {
      ctx->fps_target = target;
      ctx->frame_time_budget = 1.0 / (double) ctx->fps_target;
   } else {
      vkdf_info("Ignoring framerate target requested due to "
                "environment override.\n");
   }
}

void
vkdf_cleanup(VkdfContext *ctx);

#endif
