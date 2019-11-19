#ifndef __VKDF_CPU_PARTICLE_SOURCE_H__
#define __VKDF_CPU_PARTICLE_SOURCE_H__

#include "vkdf-deps.hpp"
#include "vkdf-box.hpp"

typedef struct _VkdfCpuParticle VkdfCpuParticle;
typedef struct _VkdfCpuParticleSource VkdfCpuParticleSource;

typedef void (*VkdfCpuParticleSourceParticleSpawnCB)(VkdfCpuParticleSource *ps,
                                                     uint32_t max_spawn_particles,
                                                     void *data);
typedef void (*VkdfCpuParticleSourceParticleUpdateCB)(VkdfCpuParticleSource *ps,
                                                      VkdfCpuParticle *p,
                                                      void *data);

struct _VkdfCpuParticle {
   glm::vec3 pos;
   glm::vec3 dir;
   float speed;
   uint32_t life;
} ;

struct _VkdfCpuParticleSource  {
   VkdfBox origin;

   glm::vec3 dir;
   float dir_noise;

   float speed;
   float speed_noise;
   float friction;

   uint32_t particle_life;
   float particle_life_noise;

   uint32_t max_particles;

   GList *particles;
   GList *particle_pool;
   uint32_t num_particles;

   bool dirty;

   struct {
       VkdfCpuParticleSourceParticleSpawnCB particle_spawn_cb;
       VkdfCpuParticleSourceParticleUpdateCB particle_update_cb;
       void *cb_data;
   } callbacks;
};

VkdfCpuParticleSource *
vkdf_cpu_particle_source_new(VkdfBox &origin,
                             glm::vec3 dir,
                             float dir_noise,
                             float speed,
                             float speed_noise,
                             float friction,
                             uint32_t particle_life,
                             float particle_life_noise,
                             uint32_t max_particles,
                             uint32_t particle_data_bytes);

void
vkdf_cpu_particle_source_set_callbacks(VkdfCpuParticleSource *ps,
                                       VkdfCpuParticleSourceParticleSpawnCB spawn_cb,
                                       VkdfCpuParticleSourceParticleUpdateCB update_cb,
                                       void *data);

VkdfCpuParticle *
vkdf_cpu_particle_source_spawn_particle(VkdfCpuParticleSource *ps);

void
vkdf_cpu_particle_source_update(VkdfCpuParticleSource *ps);

void
vkdf_cpu_particle_source_free(VkdfCpuParticleSource *ps);

#endif
