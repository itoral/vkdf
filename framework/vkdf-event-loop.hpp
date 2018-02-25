#ifndef __VKDF_EVENT_LOOP_H__
#define __VKDF_EVENT_LOOP_H__

#include "vkdf-deps.hpp"
#include "vkdf-init.hpp"

typedef void (vkdf_event_loop_update_func)(VkdfContext *ctx, void *data);
typedef void (vkdf_event_loop_render_func)(VkdfContext *ctx, void *data);

void
vkdf_event_loop_run(VkdfContext *ctx,
                    vkdf_event_loop_update_func update_func,
                    vkdf_event_loop_render_func render_func,
                    void *data);

void inline
vkdf_set_rebuild_swapchain_cbs(VkdfContext *ctx,
                               VkdfRebuildSwapChainCB before,
                               VkdfRebuildSwapChainCB after,
                               void *callback_data)
{
   ctx->before_rebuild_swap_chain_cb = before;
   ctx->after_rebuild_swap_chain_cb = after;
   ctx->rebuild_swap_chain_cb_data = callback_data;
}

void
vkdf_rebuild_swap_chain(VkdfContext *ctx);

void
vkdf_copy_to_swapchain(VkdfContext *ctx,
                       VkCommandBuffer *copy_cmd_bufs,
                       VkPipelineStageFlags wait_stage,
                       VkSemaphore wait_sem,
                       VkFence fence);

#endif

