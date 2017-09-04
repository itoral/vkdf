#ifndef __VKDF_ERROR_H__
#define __VKDF_ERROR_H__

#define S(x) str(x)
#define str(x) #x

#if ENABLE_DEBUG
   #define VK_CHECK(cmd) \
      if (cmd != VK_SUCCESS) \
         printf(__FILE__ ":" S(__LINE__) ": failed to execute vulkan command.\n");
#else
   #define VK_CHECK(cmd) cmd
#endif

void vkdf_error(const char *msg, ...);
void vkdf_fatal(const char *msg, ...);
void vkdf_info(const char *msg, ...);

#endif
