#ifndef __VKDF_INIT_H__
#define __VKDF_INIT_H__

void
vkdf_init(VkdfContext *ctx,
          uint32_t widht,
          uint32_t height,
          bool fullscreen,
          bool resizable,
          bool enable_validation);

void
vkdf_cleanup(VkdfContext *ctx);

#endif
