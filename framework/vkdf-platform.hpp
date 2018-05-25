#ifndef __VKDF_PLATFORM_H__
#define __VKDF_PLATFORM_H__

#include "vkdf-deps.hpp"

#ifdef VKDF_PLATFORM_SDL
typedef SDL_Window *VkdfWindow;
#else
typedef GLFWwindow *VkdfWindow;
#endif

typedef struct {
   VkdfWindow window;
   VkSurfaceKHR surface;
#ifdef VKDF_PLATFORM_SDL
   struct {
      SDL_Renderer *renderer;
   } sdl;
#endif
} VkdfPlatform;

typedef enum {
   VKDF_KEY_UP = 0,
   VKDF_KEY_DOWN,
   VKDF_KEY_RIGHT,
   VKDF_KEY_LEFT,
   VKDF_KEY_PAGE_UP,
   VKDF_KEY_PAGE_DOWN,
   VKDF_KEY_A,
   VKDF_KEY_L,
} VkdfKey;

void
vkdf_platform_init();

void
vkdf_platform_create_window(VkdfPlatform *platform,
                            VkInstance inst,
                            uint32_t width, uint32_t height,
                            bool fullscreen, bool resizable);

const char **
vkdf_platform_get_required_extensions(uint32_t *count);

void
vkdf_platform_finish(VkdfPlatform *platform);

double
vkdf_platform_get_time();

void
vkdf_platform_get_window_size(VkdfPlatform *platform,
                              int32_t *width, int32_t *height);

void
vkdf_platform_get_framebuffer_size(VkdfPlatform *platform,
                                   int32_t *width, int32_t *height);

bool
vkdf_platform_should_quit(VkdfPlatform *platform);

void
vkdf_platform_poll_events();

bool
vkdf_platform_key_is_pressed(VkdfPlatform *platform, VkdfKey key);

#endif
