struct Light
{
   vec4 pos;
   vec4 diffuse;
   vec4 ambient;
   vec4 specular;
   vec4 attenuation;
   vec4 rotation;
   vec4 direction;
   float spot_cutoff;
   float spot_cutoff_angle;
   float spot_angle_dist_factor;
   float spot_ambient_clamp_factor;
   float intensity;
   bool casts_shadows;
   bool dirty;
   bool dirty_shadows;
};

struct Material
{
   vec4 diffuse;
   vec4 ambient;
   vec4 specular;
   float shininess;
   float pad1, pad2, pad3;
};

struct LightColor
{
   vec3 diffuse;
   vec3 ambient;
   vec3 specular;
};

void
compute_spotlight_cutoff_factor(Light l,
                                vec3 light_to_pos_norm,
                                float att_factor,
                                out float cutoff_factor,
                                out float ambient_att_factor)
{
   // Compute angle of this light beam with the spotlight's direction
   vec3 spotlight_dir_norm = normalize(vec3(l.direction));
   float dp_angle_with_light = dot(light_to_pos_norm, spotlight_dir_norm);

   float dist;
   if (dp_angle_with_light < l.spot_cutoff) {
      // This beam is outside the spotlight, cut it off
      cutoff_factor = 0.0;
   } else {
      // This beam is inside the spotlight's area
      cutoff_factor = 1.0;
   }

   // Ambient light for spotlights should also consider the angle with
   // the spotlight besides the distance to its source. The idea is that
   // fragments that are closer to the beam get more ambient light and
   // ambient light decreases progressively as the angle with the spotlight's
   // direction increases.
   //
   // We make the ambient light be at its peak right at the center of the beam
   // and it starts decreasing progressively. This is good for ambient light
   // in areas of the scene that are inside the beam bit are occluded from
   // direct exposure (they are shadowed). Without this, we would see
   // the are that falls inside the cone of light have constant ambient light,
   // outlining the spotlight's cone and that doesn't look good.
   //
   // Also, we want that lights with a larger cutoff angle (smaller cutoff
   // factor) do not get very little ambient light at the edge of the spotlight
   // compared to smaller lights, so we modulate the distance with the light's
   // cutoff factor to compensate for this.
   //
   // Finally, we clamp the result to avoid too much ambient light inside the
   // beam, again, because it would make things look bad for occluded fragments
   // inside the cone of light, which would have too much ambient light.
   dist = (1.0 - dp_angle_with_light) *
          l.spot_angle_dist_factor * l.spot_cutoff * l.spot_cutoff;
   float angle_att_factor = 1.0 / (l.attenuation.x + l.attenuation.y * dist);
   ambient_att_factor = att_factor *
                        clamp(angle_att_factor, 0.0, l.spot_ambient_clamp_factor);
}

float
compute_shadow_factor(vec4 light_space_pos,
                      sampler2DShadow shadow_map,
                      uint shadow_map_size,
                      uint pfc_size)
{
   // Convert light space position to NDC
   vec3 light_space_ndc = light_space_pos.xyz /= light_space_pos.w;

   // If the fragment is outside the light's projection then it is outside
   // the light's influence, which means it is in the shadow (notice that
   // such fragment position is outside the shadow map texture so it would
   // it be incorrect to sample the shadow map with it)
   if (abs(light_space_ndc.x) > 1.0 ||
       abs(light_space_ndc.y) > 1.0 ||
       light_space_ndc.z > 1.0)
      return 0.0;

   // Translate from NDC to shadow map space (Vulkan's Z is already in [0..1])
   vec2 shadow_map_coord = light_space_ndc.xy * 0.5 + 0.5;

   // compute total number of samples to take from the shadow map
   int pfc_size_minus_1 = int(pfc_size - 1);
   float kernel_size = 2.0 * pfc_size_minus_1 + 1.0;
   float num_samples = kernel_size * kernel_size;

   // Counter for the shadow map samples not in the shadow
   float lighted_count = 0.0;

   // Take samples from the shadow map
   float shadow_map_texel_size = 1.0 / shadow_map_size;
   for (int x = -pfc_size_minus_1; x <= pfc_size_minus_1; x++)
   for (int y = -pfc_size_minus_1; y <= pfc_size_minus_1; y++) {
      // Compute coordinate for this PFC sample
      vec3 pfc_coord =
		vec3(shadow_map_coord + vec2(x, y) * shadow_map_texel_size,
             light_space_ndc.z);

      // Check if the sample is in light or in the shadow
      lighted_count += texture(shadow_map, pfc_coord);

   }

   return lighted_count / num_samples;
}

LightColor
compute_lighting(Light l,
                 vec3 world_pos,
                 vec3 normal,
                 vec3 view_dir,
                 Material mat,
                 bool receives_shadows,
                 vec4 light_space_pos,
                 sampler2DShadow shadow_map,
                 uint shadow_map_size,
                 uint pfc_size)
{
   vec3 light_to_pos_norm;
   float att_factor;
   float cutoff_factor;
   float ambient_att_factor;

   // Check if the fragment is in the shadow
   float shadow_factor;
   if (receives_shadows) {
      shadow_factor = compute_shadow_factor(light_space_pos, shadow_map,
                                            shadow_map_size, pfc_size);
   } else {
      shadow_factor = 1.0;
   }

   if (l.pos.w == 0.0) {
      // Directional light, no attenuation, no cutoff
      light_to_pos_norm = normalize(vec3(l.pos));
      att_factor = 1.0;
      cutoff_factor = 1.0;
      ambient_att_factor = att_factor;
   } else {
      // Positional light, compute attenuation
      vec3 light_to_pos = world_pos - l.pos.xyz;
      light_to_pos_norm = normalize(light_to_pos);
      float dist = length(light_to_pos);
      att_factor = 1.0 / (l.attenuation.x + l.attenuation.y * dist +
                          l.attenuation.z * dist * dist);

      if (l.pos.w == 1.0) {
         // Point light: no cutoff, normal ambient attenuation
         cutoff_factor = 1.0f;
         ambient_att_factor = att_factor;
      } else {
         // Spotlight: cutoff, ambient attenuation factors in the angle
         compute_spotlight_cutoff_factor(l, light_to_pos_norm, att_factor,
                                         cutoff_factor, ambient_att_factor);
      }
   }

   // Compute reflection from light for this fragment
   normal = normalize(normal);
   float dp_reflection = max(0.0, dot(normal, -light_to_pos_norm));

   shadow_factor *= cutoff_factor;

   // Compute light contributions to the fragment.
   LightColor lc;
   lc.diffuse = mat.diffuse.xyz * l.diffuse.xyz * att_factor *
                dp_reflection * shadow_factor * l.intensity;
   lc.ambient = mat.ambient.xyz * l.ambient.xyz * ambient_att_factor *
                l.intensity;

   lc.specular = vec3(0);
   if (dot(normal, -light_to_pos_norm) >= 0.0) {
      vec3 reflection_dir = reflect(light_to_pos_norm, normal);
      float shine_factor = dot(reflection_dir, normalize(view_dir));
      lc.specular =
           att_factor * l.specular.xyz * mat.specular.xyz *
           pow(max(0.0, shine_factor), mat.shininess) * shadow_factor *
           l.intensity;
   }

   return lc;
}

LightColor
compute_lighting(Light l,
                 vec3 world_pos,
                 vec3 normal,
                 vec3 view_dir,
                 Material mat)
{
   vec3 light_to_pos_norm;
   float att_factor;
   float cutoff_factor;
   float ambient_att_factor;

   if (l.pos.w == 0.0) {
      // Directional light, no attenuation, no cutoff
      light_to_pos_norm = normalize(vec3(l.pos));
      att_factor = 1.0;
      cutoff_factor = 1.0;
      ambient_att_factor = att_factor;
   } else {
      // Positional light, compute attenuation
      vec3 light_to_pos = world_pos - l.pos.xyz;
      light_to_pos_norm = normalize(light_to_pos);
      float dist = length(light_to_pos);
      att_factor = 1.0 / (l.attenuation.x + l.attenuation.y * dist +
                          l.attenuation.z * dist * dist);

      if (l.pos.w == 1.0) {
         // Point light: no cutoff, normal ambient attenuation
         cutoff_factor = 1.0f;
         ambient_att_factor = att_factor;
      } else {
         // Spotlight: cutoff, ambient attenuation factors in the angle
         compute_spotlight_cutoff_factor(l, light_to_pos_norm, att_factor,
                                         cutoff_factor, ambient_att_factor);
      }
   }

   // Compute reflection from light for this fragment
   normal = normalize(normal);
   float dp_reflection = max(0.0, dot(normal, -light_to_pos_norm));

   // No shadowing
   float shadow_factor = cutoff_factor;

   // Compute light contributions to the fragment.
   LightColor lc;
   lc.diffuse = mat.diffuse.xyz * l.diffuse.xyz * att_factor *
                dp_reflection * shadow_factor * l.intensity;
   lc.ambient = mat.ambient.xyz * l.ambient.xyz * ambient_att_factor *
                l.intensity;

   lc.specular = vec3(0);
   if (dot(normal, -light_to_pos_norm) >= 0.0) {
      vec3 reflection_dir = reflect(light_to_pos_norm, normal);
      float shine_factor = dot(reflection_dir, normalize(view_dir));
      lc.specular =
           att_factor * l.specular.xyz * mat.specular.xyz *
           pow(max(0.0, shine_factor), mat.shininess) * shadow_factor *
           l.intensity;
   }

   return lc;
}
