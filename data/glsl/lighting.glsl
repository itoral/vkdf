struct Light
{
   vec4 pos;
   vec4 diffuse;
   vec4 ambient;
   vec4 specular;
   vec4 attenuation;
   vec4 rotation;
   vec4 direction;
   vec4 spot_angle_attenuation;
   float spot_cutoff;
   float spot_cutoff_angle;
   float spot_padding_0;
   float spot_padding_1;
   mat4 view_matrix;
   mat4 view_matrix_inv;
   float intensity;
   bool casts_shadows;
   float volume_scale_cap;
   uint dirty;
   uint cached;
   float pad0;
   float pad1;
   float pad2;
};

struct Material
{
   vec4 diffuse;
   vec4 ambient;
   vec4 specular;
   float shininess;
   uint diffuse_tex_count;
   uint normal_tex_count;
   uint specular_tex_count;
   uint opacity_tex_count;
   float reflectiveness;
   float roughness;
   float emission;
};

struct LightColor
{
   vec3 diffuse;
   vec3 ambient;
   vec3 specular;
};

float
compute_spotlight_cutoff_factor(Light l, vec3 light_to_pos_norm)
{
   // Compute angle of this light beam with the spotlight's direction
   vec3 spotlight_dir_norm = normalize(vec3(l.direction));
   float dp_angle_with_light = dot(light_to_pos_norm, spotlight_dir_norm);

   float cutoff_factor = 0.0;
   if (dp_angle_with_light >= l.spot_cutoff) {
      /* Beam is inside the light cone, attenuate with angular distance
       * to the center of the beam
       */
      float dist = 90.0 * (1.0 - (dp_angle_with_light - l.spot_cutoff) / (1.0 - l.spot_cutoff));
      vec3 att = l.spot_angle_attenuation.xyz;
      cutoff_factor = 1.0 / (att.x + att.y * dist + att.z * dist * dist);
   }

   return cutoff_factor;
}

float
compute_shadow_factor(vec4 light_space_pos,
                      sampler2DShadow shadow_map,
                      uint shadow_map_size,
                      uint pcf_size)
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
   int pcf_size_minus_1 = int(pcf_size - 1);
   float kernel_size = 2.0 * pcf_size_minus_1 + 1.0;
   float num_samples = kernel_size * kernel_size;

   // Counter for the shadow map samples not in the shadow
   float lighted_count = 0.0;

   // Take samples from the shadow map
   float shadow_map_texel_size = 1.0 / shadow_map_size;
   for (int x = -pcf_size_minus_1; x <= pcf_size_minus_1; x++)
   for (int y = -pcf_size_minus_1; y <= pcf_size_minus_1; y++) {
      // Compute coordinate for this PCF sample
      vec3 pcf_coord =
		vec3(shadow_map_coord + vec2(x, y) * shadow_map_texel_size,
             light_space_ndc.z);

      // Check if the sample is in light or in the shadow
      lighted_count += texture(shadow_map, pcf_coord);

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
                 uint pcf_size)
{
   vec3 light_to_pos_norm;
   float att_factor;
   float cutoff_factor;

   // Check if the fragment is in the shadow
   float shadow_factor;
   if (receives_shadows) {
      shadow_factor = compute_shadow_factor(light_space_pos, shadow_map,
                                            shadow_map_size, pcf_size);
   } else {
      shadow_factor = 1.0;
   }

   if (l.pos.w == 0.0) {
      // Directional light, no attenuation, no cutoff
      light_to_pos_norm = normalize(vec3(l.pos));
      att_factor = 1.0;
      cutoff_factor = 1.0;
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
      } else {
         cutoff_factor = compute_spotlight_cutoff_factor(l, light_to_pos_norm);
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
   lc.ambient = mat.ambient.xyz * l.ambient.xyz * att_factor * l.intensity;

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

   if (l.pos.w == 0.0) {
      // Directional light, no attenuation, no cutoff
      light_to_pos_norm = normalize(vec3(l.pos));
      att_factor = 1.0;
      cutoff_factor = 1.0;
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
      } else {
         cutoff_factor = compute_spotlight_cutoff_factor(l, light_to_pos_norm);
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
   lc.ambient = mat.ambient.xyz * l.ambient.xyz * att_factor * l.intensity;

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
