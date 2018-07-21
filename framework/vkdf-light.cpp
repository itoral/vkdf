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
                         glm::vec4 attenuation,
                         glm::vec4 angle_attenuation)
{
   VkdfLight *l = g_new0(VkdfLight, 1);
   init_light(l, diffuse, ambient, specular, attenuation);
   l->origin = pos;
   l->origin.w = VKDF_LIGHT_SPOTLIGHT;
   l->spot.angle_attenuation = angle_attenuation;
   l->spot.priv.rot = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
   l->spot.priv.dir =
      glm::vec4(vkdf_compute_viewdir(glm::vec3(l->spot.priv.rot)), 0.0f);
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

/**
 * Gets the scale that we need to apply to a unit-sized model to represent
 * the geometry of the 3D space volume affected by a light source.
 *
 * The unit model for point lights is a sphere with radius=1.0 positioned
 * at the light's origin.
 *
 * The unit model for a spotlight is a cone with height=1.0 and base
 * radius=1.0 with the tip of the cone positioned at the light's origin.
 *
 * Since directional light's reach everywhere their volume is infinite. We
 * could represent this with a cube or a sphere and a very large scale
 * but instead we simply do not provide a volume for them. Applications already
 * know that their reach is infinite so they don't need us to provide a volume
 * for them.
 */
glm::vec3
vkdf_light_get_volume_scale(VkdfLight *l)
{
   const float constant  = l->attenuation.x;
   const float linear    = l->attenuation.y;
   const float quadratic = l->attenuation.z;

   const glm::vec4 color = l->diffuse;
   const float light_max = l->intensity * MAX2(MAX2(color.r, color.g), color.b);
   const float light_cutoff = 0.01f; // The volume extends up to this intensity

   const float distance =
      (-linear + sqrtf(linear * linear - 4.0f * quadratic *
                       (constant - (1.0f / light_cutoff) * light_max))) /
      (2.0f * quadratic);

   switch (vkdf_light_get_type(l)) {
   case VKDF_LIGHT_POINT:
      return glm::vec3(distance);
   case VKDF_LIGHT_SPOTLIGHT: {
      /* The height of the cone (Z-scale) is determined by attenuation. Scale
       * XY is determined by the radius of the cone (which is function of the
       * cone's height) at its base:
       *
       * tan(ang) = radius(height) / height
       * radius(height) = tan(ang) * height
       *
       * For the unit (non-scaled) cone, the angle is 45 grad and the height
       * is the same as the radius for all values of height.
       *
       * Therefore, the scale we need to apply to the radius is:
       *
       * scale = radius_cone(distance) / radius_unit_cone(distance)
       * scale = tan(ang) * distance / distance = tan(ang)
       *
       * FIXME: We should incorporate the angle attenuation here to reduce
       *        XY scale.
       */
      float t = tanf(l->spot.cutoff_angle);
      return glm::vec3(t * distance, t * distance, distance);
   }
   default:
      assert(!"Invalid light type");
      break;
   }
}

void
vkdf_light_free(VkdfLight *light)
{
   g_free(light);
}
