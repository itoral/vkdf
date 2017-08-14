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
  float pad1, pad2, pad3;
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
compute_shadow_factor(float dp_reflection,
                      float dp_angle_with_light,
                      float cutoff,
                      vec3 shadow_map_coord,
                      sampler2D shadow_map)
{
   // If the angle with the light exceeds the light's cutoff angle
   // then we are in the shadow
   if (dp_angle_with_light <= cutoff)
      return 0.0;

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
                 vec3 shadow_map_coord,
                 sampler2D shadow_map)
{
   // Compute distance from this fragment to light source
   vec3 light_to_pos = world_pos - l.pos.xyz;
   float dist = length(light_to_pos);

   // Compute angle between light direction and world position of
   // this fragment
   vec3 light_dir_norm = normalize(vec3(l.direction));
   vec3 light_to_pos_norm = normalize(light_to_pos);
   float dp_angle_with_light = dot(light_to_pos_norm, light_dir_norm);

   // Compute reflection from light for this fragment
   normal = normalize(normal);
   float dp_reflection = max(0.0, dot(normal, -light_to_pos_norm));
   float att_factor = 1.0 / (l.attenuation.x + l.attenuation.y * dist +
                             l.attenuation.z * dist * dist);

   // Check if the fragment is in the shadow
   float shadow_factor =
      compute_shadow_factor(dp_reflection, dp_angle_with_light, l.cutoff,
                            shadow_map_coord, shadow_map);

   // Compute light contributions to the fragment. Do not attenuate
   // ambient light to make it constant across the scene.
   LightColor lc;
   lc.diffuse = mat.diffuse.xyz * l.diffuse.xyz * att_factor *
                dp_reflection * shadow_factor;
   lc.ambient = mat.ambient.xyz * l.ambient.xyz;

   lc.specular = vec3(0);
   if (dot(normal, -light_dir_norm) >= 0.0) {
      vec3 reflection_dir = reflect(light_dir_norm, normal);
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
   // Compute distance from this fragment to light source
   vec3 light_to_pos = world_pos - l.pos.xyz;
   float dist = length(light_to_pos);

   // Compute angle between light direction and world position of
   // this fragment
   vec3 light_dir_norm = normalize(vec3(l.direction));
   vec3 light_to_pos_norm = normalize(light_to_pos);

   // Compute reflection from light for this fragment
   normal = normalize(normal);
   float dp_reflection = max(0.0, dot(normal, -light_to_pos_norm));
   float att_factor = 1.0 / (l.attenuation.x + l.attenuation.y * dist +
                             l.attenuation.z * dist * dist);

   // No shadowing
   float shadow_factor = 1.0;

   // Compute light contributions to the fragment. Do not attenuate
   // ambient light to make it constant across the scene.
   LightColor lc;
   lc.diffuse = mat.diffuse.xyz * l.diffuse.xyz * att_factor *
                dp_reflection * shadow_factor;
   lc.ambient = mat.ambient.xyz * l.ambient.xyz;

   lc.specular = vec3(0);
   if (dot(normal, -light_dir_norm) >= 0.0) {
      vec3 reflection_dir = reflect(light_dir_norm, normal);
      float shine_factor = dot(reflection_dir, normalize(view_dir));
      lc.specular =
           att_factor * l.specular.xyz * mat.specular.xyz *
            pow(max(0.0, shine_factor), mat.shininess) * shadow_factor;
   }

   return lc;
}
