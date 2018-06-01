#include "vkdf-platform.hpp"
#include "vkdf-error.hpp"

static void
wait_for_window_resize(VkdfPlatform *platform, int32_t width, int32_t height)
{
   int32_t last_fb_width, last_fb_height;

   bool still_resizing = true;
   int32_t tries = 0;
   do {
      int32_t fb_width, fb_height;
      vkdf_platform_get_framebuffer_size(platform, &fb_width, &fb_height);

      if (width == fb_width && height == fb_height) {
         still_resizing = false;
      } else if (last_fb_width == fb_width && last_fb_height == fb_height) {
         /* Window size has not changed */
         tries++;
         if (tries == 3) {
            still_resizing = false;
         } else {
            /* Wait for 100 ms and query again */
            struct timespec wait_time = { 0, 100000000l };
            nanosleep(&wait_time, NULL);
         }
      } else {
         /* Window size has changed, start over */
         tries = 0;
         last_fb_width = fb_width;
         last_fb_height = fb_height;
      }
   } while (still_resizing);
}

static void platform_init(VkdfPlatform *platform);

void
vkdf_platform_init(VkdfPlatform *platform)
{
   platform_init(platform);

   /* SDL2 Image library */
   IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF);
}

#ifdef VKDF_PLATFORM_SDL
#include "vkdf-platform-sdl.cpp"
#elif defined(VKDF_PLATFORM_GLFW)
#include "vkdf-platform-glfw.cpp"
#else
#error No platform defined
#endif
