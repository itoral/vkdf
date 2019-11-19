#include "vkdf-init.hpp"
#include "vkdf-init-priv.hpp"
#include "vkdf-semaphore.hpp"

// SDL BEGIN
#include <SDL2/SDL_syswm.h>
// SDL END

static VkResult
CreateDebugReportCallbackEXT(VkInstance instance,
                             const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             VkDebugReportCallbackEXT* pCallback)
{
   PFN_vkCreateDebugReportCallbackEXT func =
      (PFN_vkCreateDebugReportCallbackEXT)
         vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
   if (!func)
      return VK_ERROR_EXTENSION_NOT_PRESENT;
   return func(instance, pCreateInfo, pAllocator, pCallback);
}

static void
DestroyDebugReportCallbackEXT(VkInstance instance,
                              VkDebugReportCallbackEXT callback,
                              const VkAllocationCallbacks* pAllocator)
{
   PFN_vkDestroyDebugReportCallbackEXT func =
      (PFN_vkDestroyDebugReportCallbackEXT)
         vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
   if (func)
      func(instance, callback, pAllocator);
}

static void
create_debug_callback(VkdfContext *ctx,
                      PFN_vkDebugReportCallbackEXT debug_cb)
{
   VkDebugReportCallbackCreateInfoEXT ci = {};
   ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
   ci.pNext = NULL;
   ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
   ci.pUserData = NULL;
   ci.pfnCallback = debug_cb;

   VkResult res =
      CreateDebugReportCallbackEXT(ctx->inst, &ci, NULL, &ctx->debug_callback);

   if (res != VK_SUCCESS)
      vkdf_error("Failed to register debug callback");
}

static void
get_required_extensions(VkdfContext *ctx, bool enable_validation)
{
   ctx->inst_extension_count = enable_validation ? 1 : 0;

   uint32_t platform_extension_count;
   const char **platform_extensions =
      vkdf_platform_get_required_extensions(&platform_extension_count);

   ctx->inst_extension_count += platform_extension_count;

   ctx->inst_extensions = g_new(char *, ctx->inst_extension_count);
   for (uint32_t i = 0; i < platform_extension_count; i++)
      ctx->inst_extensions[i] = g_strdup(platform_extensions[i]);

   if (enable_validation) {
      ctx->inst_extensions[ctx->inst_extension_count - 1] =
         g_strdup(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
   }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_cb(VkDebugReportFlagsEXT flags,
         VkDebugReportObjectTypeEXT obj_type,
         uint64_t obj,
         size_t location,
         int32_t code,
         const char* layer_prefix,
         const char* msg,
         void* data)
{
   vkdf_error("validation layer: %s\n", msg);
   return VK_FALSE;
}

static void
init_instance(VkdfContext *ctx, bool enable_validation)
{
   VkApplicationInfo app_info;
   app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
   app_info.pNext = NULL;
   app_info.pApplicationName = "VKDF";
   app_info.applicationVersion = 1;
   app_info.pEngineName = "VKDF";
   app_info.engineVersion = 1;
   app_info.apiVersion = VK_API_VERSION_1_0;

   VkInstanceCreateInfo info;
   info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
   info.pNext = NULL;
   info.flags = 0;
   info.pApplicationInfo = &app_info;

   if (enable_validation) {
      // FIXME: check that layer exists (ensures that debug ext is available)
      const char *validation_layers[] = {
          "VK_LAYER_LUNARG_standard_validation"
      };
      info.enabledLayerCount = 1;
      info.ppEnabledLayerNames = validation_layers;

   } else {
      info.enabledLayerCount = 0;
      info.ppEnabledLayerNames = NULL;
   }

   get_required_extensions(ctx, enable_validation);
   info.enabledExtensionCount = ctx->inst_extension_count;
   info.ppEnabledExtensionNames = ctx->inst_extensions;

   VkResult res = vkCreateInstance(&info, NULL, &ctx->inst);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create Vulkan instance");

   if (enable_validation)
      create_debug_callback(ctx, debug_cb);
}

static void
init_physical_device(VkdfContext *ctx)
{
   VkResult res =
      vkEnumeratePhysicalDevices(ctx->inst, &ctx->phy_device_count, NULL);

   if (res != VK_SUCCESS)
       vkdf_fatal("Failed to query the number of available Vulkan devices");

   if (ctx->phy_device_count == 0)
       vkdf_fatal("No Vulkan devices found");

   ctx->phy_devices = g_new(VkPhysicalDevice, ctx->phy_device_count);
   res = vkEnumeratePhysicalDevices(ctx->inst, &ctx->phy_device_count,
                                    ctx->phy_devices);
   if (res != VK_SUCCESS)
       vkdf_fatal("Failed to query Vulkan devices");

   if (ctx->phy_device_count > 1)
      vkdf_info("Found %u Vulkan devices. Using device 0\n",
                ctx->phy_device_count);
 
   ctx->phy_device = ctx->phy_devices[0];

   vkGetPhysicalDeviceProperties(ctx->phy_device, &ctx->phy_device_props);
   vkGetPhysicalDeviceMemoryProperties(ctx->phy_device,
                                       &ctx->phy_device_mem_props);
   vkGetPhysicalDeviceFeatures(ctx->phy_device, &ctx->phy_device_features);

   vkEnumerateDeviceExtensionProperties(ctx->phy_device, NULL,
                                        &ctx->phy_device_extension_count, NULL);

   if (ctx->phy_device_extension_count > 0) {
      ctx->phy_device_extensions = g_new(VkExtensionProperties,
                                         ctx->phy_device_extension_count);
      vkEnumerateDeviceExtensionProperties(ctx->phy_device, NULL,
                                           &ctx->phy_device_extension_count,
                                           ctx->phy_device_extensions);
   }

   vkdf_info("Device name: %s\n", ctx->phy_device_props.deviceName);
}

static void
init_queues(VkdfContext *ctx)
{
   vkGetPhysicalDeviceQueueFamilyProperties(ctx->phy_device,
                                            &ctx->queue_count, NULL);
   if (ctx->queue_count == 0)
      vkdf_fatal("Failed to query the number of available queues");

   ctx->queues = g_new(VkQueueFamilyProperties, ctx->queue_count);

   vkGetPhysicalDeviceQueueFamilyProperties(
      ctx->phy_device, &ctx->queue_count, ctx->queues);
   if (ctx->queue_count == 0)
      vkdf_fatal("Selected Vulkan device does not expose any queues");

   bool *can_present = g_new(bool, ctx->queue_count);
   for (uint32_t i = 0; i < ctx->queue_count; i++) {
      // GLFW does not call vkGetPhysicalDeviceSurfaceSupportKHR and it should.
      // See: https://github.com/glfw/glfw/issues/828
      // can_present[i] =
      //    glfwGetPhysicalDevicePresentationSupport(ctx->inst, ctx->phy_device, i);
      if (!ctx->no_swapchain) {
         vkGetPhysicalDeviceSurfaceSupportKHR(ctx->phy_device,
                                              i, ctx->platform.surface,
                                              (VkBool32 *) &can_present[i]);
      } else {
         can_present[i] = false;
      }
   }

   int32_t gfx_queue_index = -1;
   int32_t pst_queue_index = -1;
   for (uint32_t i = 0; i < ctx->queue_count; i++) {
      if (ctx->queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
         if (gfx_queue_index == -1)
            gfx_queue_index = i;
         if (can_present[i]) {
            gfx_queue_index = i;
            pst_queue_index = i;
            break;
         }
      }
   }

   if (gfx_queue_index == -1)
      vkdf_fatal("Selected device does not provide a graphics queue");

   if (ctx->pst_queue_index == -1) {
      for (uint32_t i = 0; i < ctx->queue_count; i++) {
         if (can_present[i]) {
            ctx->pst_queue_index = i;
            break;
         }
      }
   }

   g_free(can_present);

   if (pst_queue_index == -1 && !ctx->no_swapchain)
      vkdf_fatal("Selected device does not provide a presentation queue");

   ctx->gfx_queue_index = (uint32_t) gfx_queue_index;
   ctx->pst_queue_index = (uint32_t) pst_queue_index;
}

static bool
check_extension_supported(VkdfContext *ctx, const char *ext)
{
   uint32_t len = strlen(ext);
   uint32_t idx = 0;
   for (; idx < ctx->phy_device_extension_count; idx++) {
      if (!strncmp(ext, ctx->phy_device_extensions[idx].extensionName, len)) {
         break;
      }
   }

   return idx < ctx->phy_device_extension_count;
}

struct _extension_spec {
   const char *name;
   bool required;
};

static void
choose_device_extensions(VkdfContext *ctx,
                         const char ***enabled_extensions,
                         uint32_t *enabled_extension_count)
{
   /* List of extensions to check.
    *
    * NOTE: *must* be in the same order as the fields in the
    *       'device_extensions' union.
    */
   static struct _extension_spec extensions[] = {
      { "VK_KHR_swapchain",            true},
      { "VK_KHR_maintenance1",         true},
   };

   const uint32_t num_extensions = sizeof(extensions) / sizeof(extensions[0]);

   /* Sanity check: we must have the same number of extensions here than we
    * have in the device_extensions union.
    */
   assert(num_extensions ==
          sizeof(ctx->device_extensions) / sizeof(ctx->device_extensions.enabled[0]));

   /* Build the list of extensions to enable */
   memset(&ctx->device_extensions, 0, sizeof(ctx->device_extensions));

   *enabled_extensions = g_new0(const char *, num_extensions);
   *enabled_extension_count = 0;

   for (uint32_t i = 0; i < num_extensions; i++) {
      const char *ext = extensions[i].name;
      if (!check_extension_supported(ctx, ext)) {
         if (extensions[i].required)
            vkdf_fatal("Required extension '%s' not available.\n", ext);
         else
            vkdf_info("Optional extension '%s' not available.\n", ext);
      } else {
         ctx->device_extensions.enabled[i] = true;
         (*enabled_extensions)[(*enabled_extension_count)++] = ext;
      }
   }
}

static void
choose_device_features(VkdfContext *ctx)
{
   memset(&ctx->device_features, 0, sizeof(ctx->device_features));

   /* Anisotropic filtering */
   ctx->device_features.samplerAnisotropy =
      ctx->phy_device_features.samplerAnisotropy;

   /* Depth clamp
    *
    * We can use depth clamp for rendering light volumes during a deferred
    * lighting pass, to prevent the light volumes from being Z-clipped.
    */
   ctx->device_features.depthClamp = ctx->phy_device_features.depthClamp;
}

static void
init_logical_device(VkdfContext *ctx)
{
   float queue_priorities[1] = { 0.0f };
   VkDeviceQueueCreateInfo queue_info;
   queue_info.queueFamilyIndex = ctx->gfx_queue_index;
   queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
   queue_info.pNext = NULL;
   queue_info.flags = 0;
   queue_info.queueCount = 1;
   queue_info.pQueuePriorities = queue_priorities;

   /* Choose extensions and features to enable */
   const char **extension_names;
   uint32_t extension_count;
   choose_device_extensions(ctx, &extension_names, &extension_count);
   choose_device_features(ctx);

   /* Create logical device */
   VkDeviceCreateInfo device_info;
   device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   device_info.pNext = NULL;
   device_info.flags = 0;   
   device_info.queueCreateInfoCount = 1;
   device_info.pQueueCreateInfos = &queue_info;
   device_info.enabledExtensionCount = extension_count;
   device_info.ppEnabledExtensionNames = extension_names;
   device_info.enabledLayerCount = 0;
   device_info.ppEnabledLayerNames = NULL;
   device_info.pEnabledFeatures = &ctx->device_features;

   VkResult res =
      vkCreateDevice(ctx->phy_device, &device_info, NULL, &ctx->device);
   if (res != VK_SUCCESS) 
      vkdf_fatal("Could not create Vulkan logical device.\n");

   vkGetDeviceQueue(ctx->device, ctx->gfx_queue_index, 0, &ctx->gfx_queue);

   // FIXME: handle separate queue for presentation
   assert(ctx->gfx_queue_index == ctx->pst_queue_index ||
          ctx->pst_queue_index == -1);
   ctx->pst_queue = ctx->gfx_queue;

   g_free(extension_names);
}

static void
init_window_surface(VkdfContext *ctx, uint32_t width, uint32_t height,
                    bool fullscreen, bool resizable)
{
   VkResult res;

   ctx->width = width;
   ctx->height = height;

   if (ctx->no_swapchain)
      return;

   vkdf_platform_create_window(&ctx->platform, ctx->inst,
                               width, height, fullscreen, resizable);

   uint32_t num_formats;
   res = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->phy_device,
                                              ctx->platform.surface,
                                              &num_formats, NULL);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to query surface formats");

   VkSurfaceFormatKHR *formats = g_new(VkSurfaceFormatKHR, num_formats);
   res = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->phy_device,
                                              ctx->platform.surface,
                                              &num_formats, formats);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to query surface formats\n");

   /* If the format list includes just one entry of VK_FORMAT_UNDEFINED,
    * the surface has no preferred format and we can use any valid VkFormat.
    * Otherwise, at least one supported format will be returned, in which
    * case we choose a valid sRGB format if available.
    */
   if (num_formats >= 1) {
      uint32_t idx = 0;
      for (; idx < num_formats; idx++) {
         if ((formats[idx].format == VK_FORMAT_R8G8B8A8_SRGB ||
              formats[idx].format == VK_FORMAT_B8G8R8A8_SRGB) &&
             formats[idx].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            break;
         }
      }

      if (idx == num_formats) {
         vkdf_error("Presentation engine doesn't support any sRGB8 formats, "
                     "color output will not be correct.\n");
         ctx->surface_format = formats[0];
      }

      ctx->surface_format = formats[idx];
   } else {
      vkdf_info("Presentation engine has no preferred format, using sRGB8\n");
      ctx->surface_format.format = VK_FORMAT_R8G8B8A8_SRGB;
      ctx->surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
   }

   g_free(formats);
}

static void
destroy_swap_chain(VkdfContext *ctx)
{
   if (ctx->no_swapchain)
      return;

   for (uint32_t i = 0; i < ctx->swap_chain_length; i++) {
      vkDestroySemaphore(ctx->device, ctx->acquired_sem[i], NULL);
      vkDestroySemaphore(ctx->device, ctx->draw_sem[i], NULL);
      vkDestroyImageView(ctx->device, ctx->swap_chain_images[i].view, NULL);
   }
   g_free(ctx->acquired_sem);
   g_free(ctx->draw_sem);
   vkDestroySwapchainKHR(ctx->device, ctx->swap_chain, NULL);
   g_free(ctx->swap_chain_images);
}

static bool
present_mode_from_string(const char *str, VkPresentModeKHR *mode)
{
   if (!strcmp(str, "fifo")) {
      *mode = VK_PRESENT_MODE_FIFO_KHR;
      return true;
   }

   if (!strcmp(str, "fifo_relaxed")) {
      *mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
      return true;
   }

   if (!strcmp(str, "mailbox")) {
      *mode = VK_PRESENT_MODE_MAILBOX_KHR;
      return true;
   }

   if (!strcmp(str, "immediate")) {
      *mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
      return true;
   }

   return false;
}

static void
override_present_mode_from_env(VkdfContext *ctx, VkPresentModeKHR *mode)
{
    VkResult res;

   // Get presentation mode from environment variable
   char *env_str = getenv("VKDF_PRESENT_MODE");
   if (!env_str)
      return;

   VkPresentModeKHR env_mode;
   if (!present_mode_from_string(env_str, &env_mode)) {
      vkdf_error("Ignoring Unknown presentation mode '%s'.\n", env_str);
      return;
   }

   // Override presentation mode if requested mode is supported
   uint32_t mode_count;
   res = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->phy_device,
                                                   ctx->platform.surface,
                                                   &mode_count, NULL);
   if (res != VK_SUCCESS) {
      vkdf_error("Failed to query available presentation modes.\n");
      return;
   }

   VkPresentModeKHR *modes = g_new(VkPresentModeKHR, mode_count);
   res = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->phy_device,
                                                   ctx->platform.surface,
                                                   &mode_count, modes);
   if (res != VK_SUCCESS) {
      g_free(modes);
      vkdf_fatal("Failed to query surface surface presentation modes.\n");
      return;
   }

   for (uint32_t i = 0; i < mode_count; i++) {
      if (modes[i] == env_mode) {
         *mode = env_mode;
         break;
      }
   }

   if (*mode == env_mode) {
      vkdf_info("Overriding presentation mode from environment variable "
                "to '%s'.\n", env_str);
   } else {
      vkdf_error("Can't override presentation mode: '%s' is not supported.\n",
                 env_str);
   }

   g_free(modes);
}


void
_init_swap_chain(VkdfContext *ctx)
{
   if (ctx->no_swapchain)
      return;

   VkResult res;

   if (ctx->swap_chain_length > 0)
      destroy_swap_chain(ctx);

   // Query surface capabilities
   res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->phy_device,
                                                   ctx->platform.surface,
                                                   &ctx->surface_caps);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to query surface capabilities");

   VkSurfaceCapabilitiesKHR caps = ctx->surface_caps;

   VkExtent2D swap_chain_ext;
   if (caps.currentExtent.width == 0xFFFFFFFF) {
      // Undefined surface size, use context dimensions
      swap_chain_ext.width = ctx->width;
      swap_chain_ext.height = ctx->height;

      if (swap_chain_ext.width < caps.minImageExtent.width)
         swap_chain_ext.width = caps.minImageExtent.width;
      else if (swap_chain_ext.width > caps.maxImageExtent.width)
         swap_chain_ext.width = caps.maxImageExtent.width;

      if (swap_chain_ext.height < caps.minImageExtent.height)
         swap_chain_ext.height = caps.minImageExtent.height;
      else if (swap_chain_ext.height > caps.maxImageExtent.height)
         swap_chain_ext.height = caps.maxImageExtent.height;
   } else {
      swap_chain_ext = caps.currentExtent;
      ctx->width = swap_chain_ext.width;
      ctx->height = swap_chain_ext.height;
   }

   // Choose presentation mode, we go with FIFO by default but let applications
   // chose a different method via environment variable.
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
   override_present_mode_from_env(ctx, &present_mode);

   // Use triple-buffering if available
   uint32_t swap_chain_size = caps.minImageCount + 1;
   if (caps.maxImageCount > 0 && swap_chain_size > caps.maxImageCount)
      swap_chain_size = caps.maxImageCount;

   // Presentation transform
   VkSurfaceTransformFlagBitsKHR present_transform;
   if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
      present_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   else
      present_transform = caps.currentTransform;

   // Create swap chain
   VkSwapchainCreateInfoKHR swap_chain_info;
   swap_chain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
   swap_chain_info.pNext = NULL;
   swap_chain_info.surface = ctx->platform.surface;
   swap_chain_info.minImageCount = swap_chain_size;
   swap_chain_info.imageFormat = ctx->surface_format.format;
   swap_chain_info.imageExtent.width = swap_chain_ext.width;
   swap_chain_info.imageExtent.height = swap_chain_ext.height;
   swap_chain_info.preTransform = present_transform;
   swap_chain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
   swap_chain_info.imageArrayLayers = 1;
   swap_chain_info.presentMode = present_mode;
   swap_chain_info.oldSwapchain = NULL;
   swap_chain_info.clipped = true;
   swap_chain_info.imageColorSpace = ctx->surface_format.colorSpace;
   swap_chain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   swap_chain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
   swap_chain_info.queueFamilyIndexCount = 0;
   swap_chain_info.pQueueFamilyIndices = NULL;
   swap_chain_info.flags = 0;

   if (ctx->gfx_queue_index != ctx->pst_queue_index) {
      // If the graphics and present queues are from different queue families,
      // we either have to explicitly transfer ownership of images between
      // the queues, or we have to create the swap_chain with imageSharingMode
      // as VK_SHARING_MODE_CONCURRENT
      //
      uint32_t queue_indices[2] = {
         (uint32_t) ctx->gfx_queue_index,
         (uint32_t) ctx->pst_queue_index
      };
      swap_chain_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      swap_chain_info.queueFamilyIndexCount = 2;
      swap_chain_info.pQueueFamilyIndices = queue_indices;
   }

   res = vkCreateSwapchainKHR(ctx->device, &swap_chain_info, NULL,
                              &ctx->swap_chain);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create swap chain");

   // Retrieve images from the swap chain
   res = vkGetSwapchainImagesKHR(ctx->device, ctx->swap_chain,
                                 &ctx->swap_chain_length, NULL);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to query number of images in the swap chain");

   VkImage *images = g_new(VkImage, ctx->swap_chain_length);
   res = vkGetSwapchainImagesKHR(ctx->device, ctx->swap_chain,
                                 &ctx->swap_chain_length, images);

   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to retrieve images in the swap chain");

   ctx->swap_chain_images = g_new(VkdfSwapChainImage, ctx->swap_chain_length);
   for (uint32_t i = 0; i < ctx->swap_chain_length; i++) {
      ctx->swap_chain_images[i].image = images[i];

      VkImageViewCreateInfo image_view = {};
      image_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      image_view.pNext = NULL;
      image_view.flags = 0;
      image_view.image = ctx->swap_chain_images[i].image;
      image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      image_view.format = ctx->surface_format.format;
      image_view.components.r = VK_COMPONENT_SWIZZLE_R;
      image_view.components.g = VK_COMPONENT_SWIZZLE_G;
      image_view.components.b = VK_COMPONENT_SWIZZLE_B;
      image_view.components.a = VK_COMPONENT_SWIZZLE_A;
      image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      image_view.subresourceRange.baseMipLevel = 0;
      image_view.subresourceRange.levelCount = 1;
      image_view.subresourceRange.baseArrayLayer = 0;
      image_view.subresourceRange.layerCount = 1;

      res = vkCreateImageView(ctx->device, &image_view, NULL,
                              &ctx->swap_chain_images[i].view);
      if (res != VK_SUCCESS)
         vkdf_fatal("Failed to create image views for the swap chain images");
   }

   g_free(images);

   // Create swap chain acquisition and rendering sync primitives
   ctx->acquired_sem = g_new(VkSemaphore, ctx->swap_chain_length);
   ctx->draw_sem = g_new(VkSemaphore, ctx->swap_chain_length);
   for (uint32_t i = 0; i < ctx->swap_chain_length; i++) {
      ctx->acquired_sem[i] = vkdf_create_semaphore(ctx);
      ctx->draw_sem[i] = vkdf_create_semaphore(ctx);
   }

   // Set the initial chain index to the last image, so the first time
   // we call acquire we circle it back to index 0.
   ctx->swap_chain_index = ctx->swap_chain_length - 1;
}

static void
set_fps_target_from_env(VkdfContext *ctx)
{
   char *env_str = getenv("VKDF_FPS_TARGET");
   if (!env_str)
      return;

   char *last;
   float fps = strtod(env_str, &last);
   if (*last != '\0' || fps <= 0.0) {
      vkdf_error("Can't set target fps from environment variable "
                 "with value '%s'\n", env_str);
      return;
   }

   vkdf_info("Setting fps target from environment variable to %.2f.\n", fps);
   vkdf_set_framerate_target(ctx, fps);

   ctx->fps_target_from_env = true;
}

void
vkdf_init(VkdfContext *ctx,
          uint32_t width,
          uint32_t height,
          bool fullscreen,
          bool resizable,
          bool enable_validation)
{
   if (getenv("VKDF_HOME") == NULL)
      vkdf_fatal("VKDF_HOME environment variable is not set.");

   memset(ctx, 0, sizeof(VkdfContext));

   ctx->no_swapchain = getenv("VKDF_NO_SWAPCHAIN") != NULL;

   vkdf_platform_init(&ctx->platform);

   init_instance(ctx, enable_validation);
   init_physical_device(ctx);
   init_window_surface(ctx, width, height, fullscreen, resizable);
   init_queues(ctx);
   init_logical_device(ctx);
   _init_swap_chain(ctx);

   set_fps_target_from_env(ctx);
}

static void
destroy_instance(VkdfContext *ctx)
{
   if (ctx->debug_callback)
      DestroyDebugReportCallbackEXT(ctx->inst, ctx->debug_callback, NULL);
   vkDestroyInstance(ctx->inst, NULL);
}

static void
destroy_device(VkdfContext *ctx)
{
   vkDestroyDevice(ctx->device, NULL);
}

static void
destroy_physical_device_list(VkdfContext *ctx)
{
   g_free(ctx->phy_devices);
}

static void
destroy_queue_list(VkdfContext *ctx)
{
   g_free(ctx->queues);
}

static void
destroy_instance_extension_list(VkdfContext *ctx)
{
   for (uint32_t i = 0; i < ctx->inst_extension_count; i++)
      g_free(ctx->inst_extensions[i]);
   g_free(ctx->inst_extensions);
}

static void
destroy_physical_device_extension_list(VkdfContext *ctx)
{
   g_free(ctx->phy_device_extensions);
}

void
vkdf_cleanup(VkdfContext *ctx)
{
   destroy_swap_chain(ctx);
   destroy_device(ctx);
   destroy_physical_device_list(ctx);
   destroy_queue_list(ctx);
   destroy_instance_extension_list(ctx);
   destroy_physical_device_extension_list(ctx);
   if (!ctx->no_swapchain)
      vkDestroySurfaceKHR(ctx->inst, ctx->platform.surface, NULL);
   vkdf_platform_finish(&ctx->platform);
   destroy_instance(ctx);
}
