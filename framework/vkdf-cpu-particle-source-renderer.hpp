#ifndef __VKDF_CPU_PARTICLE_SOURCE_RENDERER_H__
#define __VKDF_CPU_PARTICLE_SOURCE_RENDERER_H__

#include "vkdf-cpu-particle-source.hpp"
#include "vkdf-mesh.hpp"

typedef struct {
   VkdfCpuParticleSource *ps;
   VkdfMesh *mesh;
   VkPipeline pipeline;
   VkPipelineLayout pipeline_layout;
   uint32_t num_descriptor_sets;
   VkDescriptorSet *descriptor_sets;
} VkdfCpuParticleSourceRenderer;

VkdfCpuParticleSourceRenderer *
vkdf_cpu_particle_source_renderer_new(VkdfCpuParticleSource *ps,
                                      VkdfMesh *mesh,
                                      VkPipeline pipeline,
                                      VkPipelineLayout pipeline_layout,
                                      uint32_t num_descriptor_sets,
                                      VkDescriptorSet *descriptor_sets);

void
vkdf_cpu_particle_source_renderer_render(VkdfCpuParticleSourceRenderer *psr,
                                         VkCommandBuffer cmd_buf);

void
vkdf_cpu_particle_source_renderer_free(VkdfCpuParticleSourceRenderer *psr);

#endif
