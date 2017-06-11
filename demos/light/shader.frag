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

const int NUM_LIGHTS = 4;
layout(std140, set = 1, binding = 0) uniform lights_ubo {
     Light lights[NUM_LIGHTS];
} Lights;

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec4 in_world_pos;

layout(location = 0) out vec4 out_color;

void main()
{
   vec4 total = vec4(0.0, 0.0, 0.0, 0.0);

   for (int i = 0; i < NUM_LIGHTS; i++) {
      Light l = Lights.lights[i];

      float dist = length(l.pos.xyz - in_world_pos.xyz);

      float att_factor = 1.0 / (l.attenuation.x + l.attenuation.y * dist +
                                l.attenuation.z * dist * dist);

      vec4 ambient = in_color * l.ambient * att_factor;
      vec4 diffuse = in_color * l.diffuse * att_factor;
      vec4 specular = in_color * l.specular * att_factor;

      /* FIXME: add specular */
      total += ambient + diffuse;
   }

   out_color = vec4(total.x, total.y, total.z, 1.0);
}
