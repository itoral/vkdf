#include "vkdf-platform.hpp"
#include "vkdf-error.hpp"

static const uint32_t glfw_key_map[] = {
   GLFW_KEY_UP,
   GLFW_KEY_DOWN,
   GLFW_KEY_RIGHT,
   GLFW_KEY_LEFT,
   GLFW_KEY_PAGE_UP,
   GLFW_KEY_PAGE_DOWN,
   GLFW_KEY_A,
   GLFW_KEY_L,
};

static void
platform_init(VkdfPlatform *platform)
{
   if (!glfwInit())
      vkdf_fatal("Failed to initialize GLFW platforms");

   if (!glfwVulkanSupported())
      vkdf_fatal("GLFW Vulkan support unavailable");
}

void
vkdf_platform_create_window(VkdfPlatform *platform,
                            VkInstance inst,
                            uint32_t width, uint32_t height,
                            bool fullscreen, bool resizable)
{
   /* Window */
   assert(width > 0 &&  height > 0);

   glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

   if (!resizable)
      glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

   platform->window =
      glfwCreateWindow(width, height, "VKDF (GLFW)",
                       fullscreen ? glfwGetPrimaryMonitor() : NULL,
                       NULL);

   glfwSetWindowSizeLimits(platform->window,
                           resizable ? 1 : width,
                           resizable ? 1 : height,
                           resizable ? GLFW_DONT_CARE : width,
                           resizable ? GLFW_DONT_CARE : height);

   if (!platform->window)
      vkdf_fatal("Failed to create window");

   /* Specially in fullscreen mode, the size of the window may change a few
    * times before it gets to its final size. This means that it can take
    * some time until the window surface we create below reaches its final
    * size. If we ignore this, by the time we init our swapchin we may
    * find that the reported surface size is smaller and initialize a
    * swapchain with smaller images than we should. Also, since we store that
    * size in the VkdfContext instance and we typically use this to define
    * our viewport/scissor rectangles it means that we end up rendering to
    * a smaller area. When this happens, we also get very noticeable screen
    * tearing even in vsync presentation modes like FIFO or MAILBOX.
    */
   wait_for_window_resize(platform, width, height);

   /* Surface */
   VkResult res =
      glfwCreateWindowSurface(inst, platform->window, NULL, &platform->surface);

   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create window surface");
}


const char **
vkdf_platform_get_required_extensions(uint32_t *count)
{
   const char **ext = glfwGetRequiredInstanceExtensions(count);
   if (!ext)
      vkdf_fatal("Required GLFW instance extensions not available");
   return ext;
}

void
vkdf_platform_finish(VkdfPlatform *platform)
{
   glfwDestroyWindow(platform->window);
   glfwTerminate();
}

double
vkdf_platform_get_time()
{
   return glfwGetTime();
}

void
vkdf_platform_get_window_size(VkdfPlatform *platform,
                              int32_t *width, int32_t *height)
{
   glfwGetWindowSize(platform->window, width, height);
}

void
vkdf_platform_get_framebuffer_size(VkdfPlatform *platform,
                                   int32_t *width, int32_t *height)
{
   glfwGetFramebufferSize(platform->window, width, height);
}

bool
vkdf_platform_should_quit(VkdfPlatform *platform)
{
   return glfwGetKey(platform->window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
          glfwWindowShouldClose(platform->window);
}

void
vkdf_platform_poll_events(VkdfPlatform *platform)
{
   glfwPollEvents();
}

bool
vkdf_platform_key_is_pressed(VkdfPlatform *platform, VkdfKey key)
{
   return glfwGetKey(platform->window, glfw_key_map[key]) == GLFW_PRESS;
}

bool
vkdf_platform_joy_enabled(VkdfPlatform *platform)
{
   return false;
}

float
vkdf_platform_joy_check_axis(VkdfPlatform *platform, VkdfJoyAxis axis)
{
   vkdf_error("Platform GLFW3: joystick support not implemented.\n");
   return false;
}

bool
vkdf_platform_joy_check_button(VkdfPlatform *platform, VkdfJoyButton btn)
{
   vkdf_error("Platform GLFW3: joystick support not implemented.\n");
   return false;
}
