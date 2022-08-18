#include "vkdf-platform.hpp"
#include "vkdf-error.hpp"

#include <SDL2/SDL_syswm.h>

static const uint32_t sdl_key_map[] = {
   SDL_SCANCODE_UP,
   SDL_SCANCODE_DOWN,
   SDL_SCANCODE_RIGHT,
   SDL_SCANCODE_LEFT,
   SDL_SCANCODE_PAGEUP,
   SDL_SCANCODE_PAGEDOWN,
   SDL_SCANCODE_SPACE,
   SDL_SCANCODE_RETURN,
   SDL_SCANCODE_A,
   SDL_SCANCODE_L,
};

static void
platform_init(VkdfPlatform *platform)
{
   if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0)
      vkdf_fatal("Failed to initialize SDL2 platform SDL_Error:%s", SDL_GetError());

   if (SDL_NumJoysticks() > 0) {
      SDL_Joystick *joy = SDL_JoystickOpen(0);
      if (!joy) {
         vkdf_error("Failed to initialize joystick 0\n");
      } else {
         platform->sdl.joy.joy = joy;
         platform->sdl.joy.num_axes = SDL_JoystickNumAxes(joy);
         platform->sdl.joy.num_buttons = SDL_JoystickNumButtons(joy);

         vkdf_info("Found joystick: '%s' with %d axes and %d buttons.\n",
                   SDL_JoystickNameForIndex(0),
                   platform->sdl.joy.num_axes,
                   platform->sdl.joy.num_buttons);

         SDL_JoystickEventState(SDL_ENABLE);
      }
   }
}

void
vkdf_platform_create_window(VkdfPlatform *platform,
                            VkInstance inst,
                            uint32_t width, uint32_t height,
                            bool fullscreen, bool resizable)
{
   /* Window */
   assert(width > 0 &&  height > 0);

   platform->window =
      SDL_CreateWindow("VKDF (SDL)", 0, 0, width, height,
                       fullscreen ? SDL_WINDOW_FULLSCREEN : 0 |
                       resizable  ? SDL_WINDOW_RESIZABLE  : 0);

   if (!platform->window)
      vkdf_fatal("Failed to create window: %s", SDL_GetError());

   platform->sdl.renderer =
      SDL_CreateRenderer(platform->window, -1, SDL_RENDERER_ACCELERATED);



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
   VkResult res = VK_SUCCESS;

   SDL_SysWMinfo win_info;
   SDL_VERSION(&win_info.version);
   SDL_GetWindowWMInfo(platform->window, &win_info);

   switch(win_info.subsystem) {
   case SDL_SYSWM_X11: {
      VkXlibSurfaceCreateInfoKHR create_info;
      create_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
      create_info.pNext = NULL;
      create_info.flags = 0;
      create_info.dpy = win_info.info.x11.display;
      create_info.window = win_info.info.x11.window;
      res = vkCreateXlibSurfaceKHR(inst, &create_info, NULL, &platform->surface);
      break;
   }
   case SDL_SYSWM_WAYLAND: {
      VkWaylandSurfaceCreateInfoKHR create_info;
      create_info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
      create_info.pNext = NULL;
      create_info.flags = 0;
      create_info.display = win_info.info.wl.display;
      create_info.surface = win_info.info.wl.surface;
      res = vkCreateWaylandSurfaceKHR(inst, &create_info, NULL, &platform->surface);
      break;
   }
   default:
      vkdf_fatal("SDL win_info.subsystem not supported: %i\n", win_info.subsystem);
   }

   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create window surface. Error code: %i", res);
}


const char **
vkdf_platform_get_required_extensions(uint32_t *count)
{
   static const char *ext[] = {
      VK_KHR_SURFACE_EXTENSION_NAME,
      VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
      VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
   };
   *count = sizeof(ext) / sizeof(ext[0]);
   return ext;
}

void
vkdf_platform_finish(VkdfPlatform *platform)
{
   if (platform->sdl.joy.joy)
      SDL_JoystickClose(platform->sdl.joy.joy);
   SDL_DestroyRenderer(platform->sdl.renderer);
   SDL_DestroyWindow(platform->window);
   SDL_Quit();
}

double
vkdf_platform_get_time()
{
   return SDL_GetTicks() / 1000.0;
}

void
vkdf_platform_get_window_size(VkdfPlatform *platform,
                              int32_t *width, int32_t *height)
{
   SDL_GetWindowSize(platform->window, width, height);
}

void
vkdf_platform_get_framebuffer_size(VkdfPlatform *platform,
                                   int32_t *width, int32_t *height)
{
   SDL_GetRendererOutputSize(platform->sdl.renderer, width, height);
}

bool
vkdf_platform_should_quit(VkdfPlatform *platform)
{
   const uint8_t *keys = SDL_GetKeyboardState(NULL);
   return keys[SDL_SCANCODE_ESCAPE] != 0;
}

void
vkdf_platform_poll_events(VkdfPlatform *platform)
{
   SDL_PumpEvents();
}

bool
vkdf_platform_key_is_pressed(VkdfPlatform *platform, VkdfKey key)
{
   const uint8_t *keys = SDL_GetKeyboardState(NULL);
   return keys[sdl_key_map[key]] != 0;
}

bool
vkdf_platform_joy_enabled(VkdfPlatform *platform)
{
   return platform->sdl.joy.joy != NULL;
}

float
vkdf_platform_joy_check_axis(VkdfPlatform *platform, VkdfJoyAxis axis)
{
   assert(axis < platform->sdl.joy.num_axes);
   return SDL_JoystickGetAxis(platform->sdl.joy.joy, axis) / (-32768.0f);
}

bool
vkdf_platform_joy_check_button(VkdfPlatform *platform, VkdfJoyButton btn)
{
   assert(btn < platform->sdl.joy.num_buttons);
   return SDL_JoystickGetButton(platform->sdl.joy.joy, btn);
}

bool
vkdf_platform_mouse_enable_relative_mode(VkdfPlatform *platform)
{
   return SDL_SetRelativeMouseMode(SDL_TRUE) == 0;
}

void
vkdf_platform_mouse_delta(VkdfPlatform *platform, int32_t *x, int32_t *y)
{
   SDL_GetRelativeMouseState(x, y);
}

bool
vkdf_platform_mouse_pressed(VkdfPlatform *platform, VkdfMouseButton btn)
{
   uint32_t mask = SDL_GetRelativeMouseState(NULL, NULL);
   return (mask & SDL_BUTTON((uint32_t) btn)) != 0;
}
