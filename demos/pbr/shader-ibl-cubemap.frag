#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 in_local_pos;

layout(set = 1, binding = 0) uniform sampler2D equirect_map;

layout(location = 0) out vec4 out_color;

vec2
translate_cube_uv_to_spherical_map_uv(vec3 v)
{
   const vec2 inv_atan = vec2(0.1591, 0.3183);

   vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
   return uv * inv_atan + vec2(0.5);
}

void main()
{
   /* The environment map image has inverted Y coordinate */
   vec3 local_pos = vec3(in_local_pos.x, -in_local_pos.y, in_local_pos.z);

   vec3 cube_uv = normalize(local_pos);
   vec2 uv = translate_cube_uv_to_spherical_map_uv(cube_uv);
   vec3 color = texture(equirect_map, uv).rgb;
   out_color = vec4(color, 1.0);
}
