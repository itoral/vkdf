#include "vkdf.hpp"

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
   l->dirty = true;
   l->dirty_view_matrix = true;
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
   if (!l->dirty_view_matrix)
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

   l->dirty_view_matrix = false;
   return &l->view_matrix;
}

const glm::mat4 *
vkdf_light_get_view_matrix_inv(VkdfLight *l)
{
   if (l->dirty_view_matrix) {
      vkdf_light_get_view_matrix(l);
      l->dirty_view_matrix_inv = true;
   }

   if (l->dirty_view_matrix_inv) {
      l->view_matrix_inv = glm::inverse(l->view_matrix);
      l->dirty_view_matrix_inv = false;
   }

   return &l->view_matrix_inv;
}


void
vkdf_light_free(VkdfLight *light)
{
   g_free(light);
}
