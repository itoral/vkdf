#ifndef __VKDF_EVENT_LOOP_H__
#define __VKDF_EVENT_LOOP_H__

typedef void (vkdf_event_loop_update_func)(VkdfContext *ctx, void *data);
typedef void (vkdf_event_loop_render_func)(VkdfContext *ctx, void *data);

void vkdf_event_loop_run(VkdfContext *ctx,
                         vkdf_event_loop_update_func update_func,
                         vkdf_event_loop_render_func render_func,
                         void *data);

#endif

