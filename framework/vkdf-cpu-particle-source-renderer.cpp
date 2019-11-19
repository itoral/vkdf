#include "vkdf-cpu-particle-source-renderer.hpp"

VkdfCpuParticleSourceRenderer *
vkdf_cpu_particle_source_renderer_new(VkdfCpuParticleSource *ps,
                                      VkdfMesh *particle_mesh,
                                      VkPipeline pipeline,
                                      VkPipelineLayout pipeline_layout,
                                      uint32_t num_descriptor_sets,
                                      VkDescriptorSet *descriptor_sets)
{
   VkdfCpuParticleSourceRenderer *psr =
      g_new0(VkdfCpuParticleSourceRenderer, 1);

   psr->ps = ps;
   psr->mesh = particle_mesh;
   psr->pipeline_layout = pipeline_layout;
   psr->pipeline = pipeline;
   psr->num_descriptor_sets = num_descriptor_sets;
   psr->descriptor_sets = descriptor_sets;

   return psr;
}

void
vkdf_cpu_particle_source_renderer_render(VkdfCpuParticleSourceRenderer *psr,
                                         VkCommandBuffer cmd_buf)
{
   if (psr->ps->num_particles == 0)
      return;

   vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, psr->pipeline);

   vkCmdBindDescriptorSets(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           psr->pipeline_layout,
                           0,
                           psr->num_descriptor_sets,
                           psr->descriptor_sets,
                           0, NULL);

   if (psr->mesh) {
      const VkDeviceSize offsets[1] = { 0 };
      vkCmdBindVertexBuffers(cmd_buf, 0, 1, &psr->mesh->vertex_buf.buf, offsets);
      vkdf_mesh_draw(psr->mesh, cmd_buf, psr->ps->num_particles, 0);
      return;
   } else {
      vkCmdDraw(cmd_buf, 1, psr->ps->num_particles, 0, 0);
   }
}

void
vkdf_cpu_particle_source_renderer_free(VkdfCpuParticleSourceRenderer *psr)
{
   g_free(psr);
}

