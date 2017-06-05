#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct Light {
  vec4 pos;
  vec4 diffuse;
  vec4 ambient;
  vec4 specular;
  vec4 attenuation;
  vec4 direction;
  float cutoff;
  float pad1, pad2, pad3;
};

layout(std140, set = 1, binding = 0) uniform light_ubo {
     Light light;
} L;

layout (set = 3, binding = 0) uniform sampler2D shadow_map;

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec4 in_world_pos;
layout(location = 3) in float in_cam_dist; 
layout(location = 4) in vec4 in_shadow_map_coord;

layout(location = 0) out vec4 out_color;

float
compute_shadow_factor(float dp_reflection,
                      float dp_angle_with_light,
                      float cutoff)
{
   // If the angle with the light exceeds the light's cutoff angle
   // then we are in the shadow
   if (dp_angle_with_light <= cutoff)
      return 0.0;

   // Sample shadow map to see if this fragment is visible from the
   // the light source or not. Notice that the shadow map has been
   // recorded with depth bias to prevent self-shadowing artifacts
   if (in_shadow_map_coord.z > texture(shadow_map, in_shadow_map_coord.xy).x)
      return 0.0;

   // Fragment is in the light
   return 1.0;
}

void main()
{
   out_color = vec4(0.0, 0.0, 0.0, 1.0);

   Light l = L.light;

   // Compute distance from this fragment to light source
   vec3 light_to_pos = in_world_pos.xyz - l.pos.xyz;
   float dist = length(light_to_pos);

   // Compute angle between light direction and world position of
   // this fragment
   vec3 light_dir_norm = normalize(vec3(l.direction));
   vec3 light_to_pos_norm = normalize(light_to_pos);
   float dp_angle_with_light = dot(light_to_pos_norm, light_dir_norm);

   // Compute reflection from light for this fragment
   vec3 normal = normalize(in_normal);
   float dp_reflection = max(0.0, dot(normal, -light_to_pos_norm));
   float att_factor = 1.0 / (l.attenuation.x + l.attenuation.y * dist +
                             l.attenuation.z * dist * dist);

   // Check if the fragment is in the shadow
   float cutoff = l.cutoff.x;
   float shadow_factor =
      compute_shadow_factor(dp_reflection, dp_angle_with_light, cutoff);

   // Compute light contributions to the fragment
   vec3 diffuse = in_color.xyz * l.diffuse.xyz * att_factor *
                  dp_reflection * shadow_factor;
   vec3 ambient = in_color.xyz * l.ambient.xyz * att_factor;

   // FIXME: add specular

   // Acumulate this light's contribution
   out_color.xyz += diffuse + ambient;

   // Add the scene base ambient light
   out_color.xyz += in_color.xyz * 0.025f;
}
