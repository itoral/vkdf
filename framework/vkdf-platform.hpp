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
      struct {
         SDL_Joystick *joy;
         uint32_t num_axes;
         uint32_t num_buttons;
      } joy;
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

/* Enum for joystick axes (matches SDL2 mapping for PS3 controller) */
typedef enum {
   /* Lefth controller */
   VKDF_JOY_AXIS_LC_H = 0,
   VKDF_JOY_AXIS_LC_V = 1,

   /* Right controller */
   VKDF_JOY_AXIS_RC_H = 3,
   VKDF_JOY_AXIS_RC_V = 4,

   /* Left and right triggers */
   VKDF_JOY_AXIS_LT   = 2,
   VKDF_JOY_AXIS_RT   = 5,
} VkdfJoyAxis;

/* Enum for joystick buttons (matches SDL2 mapping for PS3 controller) */
typedef enum {
   /* Face buttons */
   VKDF_JOY_BTN_F0         = 0,
   VKDF_JOY_BTN_F1         = 1,
   VKDF_JOY_BTN_F2         = 2,
   VKDF_JOY_BTN_F3         = 3,

   /* Shoulder buttons */
   VKDF_JOY_BTN_L1         = 4,
   VKDF_JOY_BTN_R1         = 5,
   VKDF_JOY_BTN_L2         = 6,
   VKDF_JOY_BTN_R2         = 7,

   /* Select / Start buttons */
   VKDF_JOY_BTN_SELECT     = 8,
   VKDF_JOY_BTN_START      = 9,

   /* Thumbstick buttons*/
   VKDF_JOY_BTN_L3         = 11,
   VKDF_JOY_BTN_R3         = 12,

   /* D-pad buttons */
   VKDF_JOY_BTN_DPAD_UP    = 13,
   VKDF_JOY_BTN_DPAD_DOWN  = 14,
   VKDF_JOY_BTN_DPAD_LEFT  = 15,
   VKDF_JOY_BTN_DPAD_RIGHT = 16,
} VkdfJoyButton;

void
vkdf_platform_init(VkdfPlatform *platform);

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
vkdf_platform_poll_events(VkdfPlatform *platform);

bool
vkdf_platform_key_is_pressed(VkdfPlatform *platform, VkdfKey key);

bool
vkdf_platform_joy_enabled(VkdfPlatform *platform);

float
vkdf_platform_joy_check_axis(VkdfPlatform *platform, VkdfJoyAxis axis);

bool
vkdf_platform_joy_check_button(VkdfPlatform *platform, VkdfJoyButton btn);

#endif
