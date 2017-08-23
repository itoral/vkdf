struct Light
{
   vec4 pos;
   vec4 diffuse;
   vec4 ambient;
   vec4 specular;
   vec4 attenuation;
   vec4 rotation;
   vec4 direction;
   float cutoff;
   float cutoff_angle;
   float cutoff_pad1, cutoff_pad2;
   bool casts_shadows;
   float shadows_pad1, shadows_pad2, shadows_pad3;
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

float
compute_spotlight_cutoff_factor(Light l, vec3 light_to_pos_norm)
{
   // Compute angle of this light beam with the spotlight's direction
   vec3 spotlight_dir_norm = normalize(vec3(l.direction));
   float dp_angle_with_light = dot(light_to_pos_norm, spotlight_dir_norm);

   // If the angle exceeds the light's cutoff angle then the beam is cutoff
   if (dp_angle_with_light <= l.cutoff)
      return 0.0;

   return 1.0;
}

float
compute_shadow_factor(vec4 light_space_pos,
                      sampler2D shadow_map)
{
   // Convert light space position to NDC
   vec3 light_space_ndc = light_space_pos.xyz /= light_space_pos.w;

   // Translate from NDC to shadow map space (Vulkan's Z is already in [0..1])
   vec3 shadow_map_coord =
      vec3(light_space_ndc.xy * 0.5 + 0.5, light_space_ndc.z);

   // Sample shadow map to see if this fragment is visible from the
   // the light source or not. Notice that the shadow map has been
   // recorded with depth bias to prevent self-shadowing artifacts
   if (shadow_map_coord.z > texture(shadow_map, shadow_map_coord.xy).x)
      return 0.0;

   // Fragment is in the light
   return 1.0;
}

LightColor
compute_lighting(Light l,
                 vec3 world_pos,
                 vec3 normal,
                 vec3 view_dir,
                 Material mat,
                 vec4 light_space_pos,
                 sampler2D shadow_map)
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
         // Point light, no cutoff
         cutoff_factor = 1.0f;
      } else {
         // Spotlight
         cutoff_factor = compute_spotlight_cutoff_factor(l, light_to_pos_norm);
      }
   }

   // Compute reflection from light for this fragment
   normal = normalize(normal);
   float dp_reflection = max(0.0, dot(normal, -light_to_pos_norm));

   // Check if the fragment is in the shadow
   float shadow_factor =
      cutoff_factor * compute_shadow_factor(light_space_pos, shadow_map);

   // Compute light contributions to the fragment. Do not attenuate
   // ambient light to make it constant across the scene.
   LightColor lc;
   lc.diffuse = mat.diffuse.xyz * l.diffuse.xyz * att_factor *
                dp_reflection * shadow_factor;
   lc.ambient = mat.ambient.xyz * l.ambient.xyz;

   lc.specular = vec3(0);

   if (dot(normal, -light_to_pos_norm) >= 0.0) {
      vec3 reflection_dir = reflect(light_to_pos_norm, normal);
      float shine_factor = dot(reflection_dir, normalize(view_dir));
      lc.specular =
           att_factor * l.specular.xyz * mat.specular.xyz *
            pow(max(0.0, shine_factor), mat.shininess) * shadow_factor;
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
         // Point light, no cutoff
         cutoff_factor = 1.0f;
      } else {
         // Spotlight
         cutoff_factor = compute_spotlight_cutoff_factor(l, light_to_pos_norm);
      }
   }

   // Compute reflection from light for this fragment
   normal = normalize(normal);
   float dp_reflection = max(0.0, dot(normal, -light_to_pos_norm));

   // No shadowing
   float shadow_factor = cutoff_factor;

   // Compute light contributions to the fragment. Do not attenuate
   // ambient light to make it constant across the scene.
   LightColor lc;
   lc.diffuse = mat.diffuse.xyz * l.diffuse.xyz * att_factor *
                dp_reflection * shadow_factor;
   lc.ambient = mat.ambient.xyz * l.ambient.xyz;

   lc.specular = vec3(0);
   if (dot(normal, -light_to_pos_norm) >= 0.0) {
      vec3 reflection_dir = reflect(light_to_pos_norm, normal);
      float shine_factor = dot(reflection_dir, normalize(view_dir));
      lc.specular =
           att_factor * l.specular.xyz * mat.specular.xyz *
            pow(max(0.0, shine_factor), mat.shininess) * shadow_factor;
   }

   return lc;
}
