#include "vkdf.hpp"
#include "vkdf-init-priv.hpp"

#include <string.h>

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
   uint32_t glfw_ext_count;
   const char **glfw_extensions =
      glfwGetRequiredInstanceExtensions(&glfw_ext_count);
   if (!glfw_extensions)
      vkdf_fatal("Required GLFW instance extensions not available");

   ctx->inst_extension_count = glfw_ext_count;
   if (enable_validation)
      ctx->inst_extension_count++;

   ctx->inst_extensions = g_new(char *, ctx->inst_extension_count);
   for (uint32_t i = 0; i < glfw_ext_count; i++) {
      ctx->inst_extensions[i] = g_strdup(glfw_extensions[i]);
   }
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
      vkdf_fatal("Found %u Vulkan devices. Using device 0",
                 ctx->phy_device_count);
 
   ctx->phy_device = ctx->phy_devices[0];

   vkGetPhysicalDeviceProperties(ctx->phy_device, &ctx->phy_device_props);
   vkGetPhysicalDeviceMemoryProperties(ctx->phy_device,
                                       &ctx->phy_device_mem_props);
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
      vkGetPhysicalDeviceSurfaceSupportKHR(ctx->phy_device,
                                           i, ctx->surface,
                                           (VkBool32 *) &can_present[i]);
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

   if (pst_queue_index == -1)
      vkdf_fatal("Selected device does not provide a presentation queue");

   ctx->gfx_queue_index = (uint32_t) gfx_queue_index;
   ctx->pst_queue_index = (uint32_t) pst_queue_index;
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

   ctx->device_extension_count = 1;
   ctx->device_extensions = g_new0(const char *, ctx->device_extension_count);
   ctx->device_extensions[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

   VkDeviceCreateInfo device_info;
   device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   device_info.pNext = NULL;
   device_info.flags = 0;   
   device_info.queueCreateInfoCount = 1;
   device_info.pQueueCreateInfos = &queue_info;
   device_info.enabledExtensionCount = ctx->device_extension_count;
   device_info.ppEnabledExtensionNames = ctx->device_extensions;
   device_info.enabledLayerCount = 0;
   device_info.ppEnabledLayerNames = NULL;
   device_info.pEnabledFeatures = NULL;

   VkResult res =
      vkCreateDevice(ctx->phy_device, &device_info, NULL, &ctx->device);
   if (res != VK_SUCCESS) 
      vkdf_fatal("Could not create Vulkan logical device.\n");

   vkGetDeviceQueue(ctx->device, ctx->gfx_queue_index, 0, &ctx->gfx_queue);

   // FIXME: handle separate queue for presentation
   assert(ctx->gfx_queue_index == ctx->pst_queue_index);
   ctx->pst_queue = ctx->gfx_queue;
}

static void
init_window_surface(VkdfContext *ctx, uint32_t width, uint32_t height,
                    bool fullscreen, bool resizable)
{
   // Create window
   glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

   if (!resizable)
      glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

   ctx->width = width;
   ctx->height = height;

   ctx->window = glfwCreateWindow(width, height, "VKDF",
                                  fullscreen ? glfwGetPrimaryMonitor() : NULL,
                                  NULL);
   if (!ctx->window)
      vkdf_fatal("Failed to create window");

   glfwSetWindowSizeLimits(ctx->window,
                           resizable ? 1 : width,
                           resizable ? 1 : height,
                           resizable ? GLFW_DONT_CARE : width,
                           resizable ? GLFW_DONT_CARE : height);

   VkResult res =
      glfwCreateWindowSurface(ctx->inst, ctx->window, NULL, &ctx->surface);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create window surface");

   // Create surface
   uint32_t num_formats;
   res = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->phy_device, ctx->surface,
                                              &num_formats, NULL);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to query surface formats");

   VkSurfaceFormatKHR *formats = g_new(VkSurfaceFormatKHR, num_formats);
   res = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->phy_device, ctx->surface,
                                              &num_formats, formats);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to query surface formats\n");

   // If the format list includes just one entry of VK_FORMAT_UNDEFINED,
   // the surface has no preferred format. Otherwise, at least one
   // supported format will be returned.
   if (num_formats == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
       ctx->surface_format = VK_FORMAT_R8G8B8A8_UNORM;
   } else {
      assert(num_formats >= 1);
      ctx->surface_format = formats[0].format;
   }

   g_free(formats);
}

static void
destroy_swap_chain(VkdfContext *ctx)
{
   for (uint32_t i = 0; i < ctx->swap_chain_length; i++) {
      vkDestroySemaphore(ctx->device, ctx->acquired_sem[i], NULL);
      vkDestroySemaphore(ctx->device, ctx->draw_sem[i], NULL);
      vkDestroyImageView(ctx->device, ctx->swap_chain_images[i].view, NULL);
   }
   g_free(ctx->acquired_sem);
   g_free(ctx->draw_sem);
   vkDestroySwapchainKHR(ctx->device, ctx->swap_chain, NULL);
}

void
_init_swap_chain(VkdfContext *ctx)
{
   if (ctx->swap_chain_length > 0)
      destroy_swap_chain(ctx);

   // Query available presentation modes
   uint32_t present_mode_count;
   VkResult res;
   res = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->phy_device,
                                                   ctx->surface,
                                                   &present_mode_count, NULL);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to query surface surface presentation modes");

   VkPresentModeKHR *present_modes =
      g_new(VkPresentModeKHR, present_mode_count);

   res = vkGetPhysicalDeviceSurfacePresentModesKHR(
       ctx->phy_device, ctx->surface, &present_mode_count, present_modes);

   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to query surface surface presentation modes");

   // Query surface capabilities
   res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->phy_device,
                                                   ctx->surface,
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

   // Choose presentation mode, go with FIFO if present
   VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
   uint32_t pm_index;
   for (pm_index = 0; pm_index < present_mode_count; pm_index++) {
      if (present_modes[pm_index] == VK_PRESENT_MODE_FIFO_KHR)
         break;
   }
   if (pm_index >= present_mode_count)
      present_mode = present_modes[0];

   // Use triple-buffering or double-buffering if available
   uint32_t swap_chain_size = caps.minImageCount;

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
   swap_chain_info.surface = ctx->surface;
   swap_chain_info.minImageCount = swap_chain_size;
   swap_chain_info.imageFormat = ctx->surface_format;
   swap_chain_info.imageExtent.width = swap_chain_ext.width;
   swap_chain_info.imageExtent.height = swap_chain_ext.height;
   swap_chain_info.preTransform = present_transform;
   swap_chain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
   swap_chain_info.imageArrayLayers = 1;
   swap_chain_info.presentMode = present_mode;
   swap_chain_info.oldSwapchain = NULL;
   swap_chain_info.clipped = true;
   swap_chain_info.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
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
      image_view.format = ctx->surface_format;
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

void
vkdf_init(VkdfContext *ctx,
          uint32_t width,
          uint32_t height,
          bool fullscreen,
          bool resizable,
          bool enable_validation)
{
   memset(ctx, 0, sizeof(VkdfContext));

   if (!glfwInit())
      vkdf_fatal("Failed to initialize GLFW");

   if (!glfwVulkanSupported())
      vkdf_fatal("Vulkan support unavailable");

   init_instance(ctx, enable_validation);
   init_physical_device(ctx);
   init_window_surface(ctx, width, height, fullscreen, resizable);
   init_queues(ctx);
   init_logical_device(ctx);
   _init_swap_chain(ctx);
}

static void
destroy_instance(VkdfContext *ctx)
{
   if (ctx->debug_callback)
      DestroyDebugReportCallbackEXT(ctx->inst, ctx->debug_callback, NULL);
   vkDestroyInstance(ctx->inst, NULL);
}

void
vkdf_cleanup(VkdfContext *ctx)
{
   destroy_swap_chain(ctx);
   vkDestroyDevice(ctx->device, NULL);
   vkDestroySurfaceKHR(ctx->inst, ctx->surface, NULL);
   destroy_instance(ctx);
   glfwDestroyWindow(ctx->window);
   glfwTerminate();
}
