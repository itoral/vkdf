#ifndef __VKDF_INIT_H__
#define __VKDF_INIT_H__

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
   ctx->fps_target = target;
   ctx->frame_time_budget = 1.0 / (double) ctx->fps_target;
}

void
vkdf_cleanup(VkdfContext *ctx);

#endif
