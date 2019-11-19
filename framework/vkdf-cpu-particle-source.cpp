#include "vkdf-cpu-particle-source.hpp"
#include "vkdf-util.hpp"

static GList *
return_particle_to_pool(VkdfCpuParticleSource *ps, GList *link)
{
   assert(ps->num_particles > 0);
   ps->num_particles--;

   GList *next = link->next;
   ps->particles = g_list_remove_link(ps->particles, link);
   ps->particle_pool = g_list_concat(ps->particle_pool, link);
   return next;
}

static inline void
allocate_particle(VkdfCpuParticleSource *ps, uint32_t bytes)
{
   void *p = calloc(1, bytes);
   ps->particle_pool = g_list_prepend(ps->particle_pool, p);
}

static inline GList *
get_particle_from_pool(VkdfCpuParticleSource *ps)
{
   /* Pop the head of the pool */
   assert(ps->particle_pool);
   GList *head = ps->particle_pool;
   ps->particle_pool = g_list_remove_link(ps->particle_pool, head);
   return head;
}

VkdfCpuParticle *
vkdf_cpu_particle_source_spawn_particle(VkdfCpuParticleSource *ps)
{
   GList *link = get_particle_from_pool(ps);
   ps->particles = g_list_concat(ps->particles, link);
   ps->num_particles++;

   VkdfCpuParticle *p = (VkdfCpuParticle *) link->data;

   glm::vec3 pos_delta = glm::vec3(rand_float(-ps->origin.w, ps->origin.w),
                                   rand_float(-ps->origin.h, ps->origin.h),
                                   rand_float(-ps->origin.d, ps->origin.d));

   p->pos = ps->origin.center + pos_delta;

   glm::vec3 dir_delta = glm::vec3(rand_float(-1.0f, 1.0f) * ps->dir_noise,
                                   rand_float(-1.0f, 1.0f) * ps->dir_noise,
                                   rand_float(-1.0f, 1.0f) * ps->dir_noise);
   p->dir = ps->dir + dir_delta;
   vkdf_vec3_normalize(&p->dir);

   float speed_delta = rand_float(0.0f, 1.0f) * ps->speed_noise;
   p->speed = ps->speed + speed_delta;

   uint32_t life_delta = roundf(rand_float(-1.0, 0.0f) * ps->particle_life_noise);
   p->life = ps->particle_life + life_delta;

   return p;
}

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
                             uint32_t particle_data_bytes)
{
   assert(particle_data_bytes >= sizeof(VkdfCpuParticle));

   VkdfCpuParticleSource *ps = g_new0(VkdfCpuParticleSource, 1);
   ps->origin = origin;
   ps->dir = dir;
   ps->dir_noise = dir_noise;
   ps->speed = speed;
   ps->speed_noise = speed_noise;
   ps->friction = friction;
   ps->particle_life = particle_life;
   ps->particle_life_noise = particle_life_noise;
   ps->max_particles = max_particles;
   ps->particles = NULL;
   ps->num_particles = 0;
   ps->particle_pool = NULL;
   for (uint32_t i = 0; i < ps->max_particles; i++)
      allocate_particle(ps, particle_data_bytes);
   ps->dirty = true;

   return ps;
}

static void
default_particle_update(VkdfCpuParticleSource *ps,
                        VkdfCpuParticle *p,
                        void *data)
{
     p->pos += p->dir * p->speed;
     p->speed = MAX2(p->speed - ps->friction, 0.0f);
}

void
vkdf_cpu_particle_source_set_callbacks(VkdfCpuParticleSource *ps,
                                       VkdfCpuParticleSourceParticleSpawnCB spawn_cb,
                                       VkdfCpuParticleSourceParticleUpdateCB update_cb,
                                       void *data)
{
   assert(spawn_cb);

   ps->callbacks.particle_spawn_cb = spawn_cb;
   ps->callbacks.particle_update_cb = update_cb;
   ps->callbacks.cb_data = data;
}

void
vkdf_cpu_particle_source_update(VkdfCpuParticleSource *ps)
{
   ps->dirty = true;

   /* Update particles */
   GList *iter = ps->particles;
   while (iter) {
      VkdfCpuParticle *p = (VkdfCpuParticle *) iter->data;
      assert(p->life > 0);

      if (!ps->callbacks.particle_update_cb)
         default_particle_update(ps, p, NULL);
      else
         ps->callbacks.particle_update_cb(ps, p, ps->callbacks.cb_data);

      p->life--;
      if (p->life == 0)
         iter = return_particle_to_pool(ps, iter);
      else
         iter = g_list_next(iter);
   }

   /* Spawn new particles if needed */
   if (ps->max_particles <= ps->num_particles)
      return;

   uint32_t max_spawn_particles = ps->max_particles - ps->num_particles;
   ps->callbacks.particle_spawn_cb(ps, max_spawn_particles,
                                   ps->callbacks.cb_data);
}

void
vkdf_cpu_particle_source_free(VkdfCpuParticleSource *ps)
{
   /* First, move all particles back to the pool */
   GList *iter = ps->particles;
   while (iter)
      iter = return_particle_to_pool(ps, iter);

   /* Now free the pool */
   g_list_free_full(ps->particle_pool, (GDestroyNotify) g_free);
}
