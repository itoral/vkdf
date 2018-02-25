#include "vkdf-light.hpp"

static void
init_light(VkdfLight *l,
           glm::vec4 diffuse,
           glm::vec4 ambient,
           glm::vec4 specular,
           glm::vec4 attenuation)
{
   l->diffuse = diffuse;
   l->ambient = ambient;
   l->specular = specular;
   l->attenuation = attenuation;
   l->intensity = 1.0f;

   uint32_t dirty_bits = VKDF_LIGHT_DIRTY | VKDF_LIGHT_DIRTY_VIEW;
   bitfield_set(&l->dirty, dirty_bits);

   bitfield_unset(&l->cached, ~0);
}

VkdfLight *
vkdf_light_new_directional(glm::vec4 dir,
                           glm::vec4 diffuse,
                           glm::vec4 ambient,
                           glm::vec4 specular)
{
   VkdfLight *l = g_new0(VkdfLight, 1);
   init_light(l, diffuse, ambient, specular, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
   l->origin = dir;
   l->origin.w = VKDF_LIGHT_DIRECTIONAL;
   return l;
}

VkdfLight *
vkdf_light_new_positional(glm::vec4 pos,
                          glm::vec4 diffuse,
                          glm::vec4 ambient,
                          glm::vec4 specular,
                          glm::vec4 attenuation)
{
   VkdfLight *l = g_new0(VkdfLight, 1);
   init_light(l, diffuse, ambient, specular, attenuation);
   l->origin = pos;
   l->origin.w = VKDF_LIGHT_POINT;
   return l;
}

VkdfLight *
vkdf_light_new_spotlight(glm::vec4 pos,
                         float cutoff_angle,
                         glm::vec4 diffuse,
                         glm::vec4 ambient,
                         glm::vec4 specular,
                         glm::vec4 attenuation)
{
   VkdfLight *l = g_new0(VkdfLight, 1);
   init_light(l, diffuse, ambient, specular, attenuation);
   l->origin = pos;
   l->origin.w = VKDF_LIGHT_SPOTLIGHT;
   l->spot.priv.rot = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
   l->spot.priv.dir =
      glm::vec4(vkdf_compute_viewdir(glm::vec3(l->spot.priv.rot)), 0.0f);
   l->spot.angle_dist_factor = 50.0f;
   l->spot.ambient_clamp_factor = 2.0f;
   vkdf_light_set_cutoff_angle(l, cutoff_angle);
   return l;
}

const glm::mat4 *
vkdf_light_get_view_matrix(VkdfLight *l)
{
   if (bitfield_get(l->cached, VKDF_LIGHT_CACHED_VIEW))
      return &l->view_matrix;

   switch (vkdf_light_get_type(l)) {
   case VKDF_LIGHT_SPOTLIGHT:
      l->view_matrix =
         vkdf_compute_view_matrix_for_rotation(glm::vec3(l->origin),
                                               glm::vec3(l->spot.priv.rot));
      break;
   case VKDF_LIGHT_DIRECTIONAL:
      // The result needs to be translated to the shadow box center by
      // the caller
      l->view_matrix =
         vkdf_compute_view_matrix_for_direction(glm::vec3(l->origin));
      break;
   default:
      // FIXME: point lights
      assert(!"not implemented");
      break;
   };

   bitfield_unset(&l->dirty, VKDF_LIGHT_DIRTY_VIEW);

   bitfield_set(&l->cached, VKDF_LIGHT_CACHED_VIEW);
   bitfield_unset(&l->cached, VKDF_LIGHT_CACHED_VIEW_INV);

   return &l->view_matrix;
}

const glm::mat4 *
vkdf_light_get_view_matrix_inv(VkdfLight *l)
{
   if (bitfield_get(l->dirty, VKDF_LIGHT_DIRTY_VIEW))
      vkdf_light_get_view_matrix(l);

   if (!bitfield_get(l->cached, VKDF_LIGHT_CACHED_VIEW_INV)) {
      l->view_matrix_inv = glm::inverse(l->view_matrix);
      bitfield_set(&l->cached, VKDF_LIGHT_CACHED_VIEW_INV);
   }

   return &l->view_matrix_inv;
}

void
vkdf_light_free(VkdfLight *light)
{
   g_free(light);
}
