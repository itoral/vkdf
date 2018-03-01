#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform pcb {
   int is_horiz;
} PCB;

layout(set = 0, binding = 0) uniform sampler2D tex_input;
layout(set = 0, binding = 1) uniform sampler2D tex_normal; /* .w is roughness */

layout(location = 0) in vec2 in_texel_size;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main()
{
   /* Only blur pixels with reflection info */
   out_color = texture(tex_input, in_uv);
   if (out_color.a == 0.0)
      return;

   int kernel_size = int(texture(tex_normal, in_uv).w);
   if (kernel_size == 0)
      return;

   int counter = 1;
   vec4 center_color = out_color;
   vec2 multiplier = PCB.is_horiz != 0 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);

   for (int i = -kernel_size; i < 0; i++) {
      vec2 offset = in_texel_size * i;
      vec2 uv = in_uv + offset * multiplier;
      vec4 tex_color = texture(tex_input, uv);
      if (tex_color.a > 0.0) {
         out_color += tex_color;
         counter++;
      }
   }

   for (int i = 1; i <= kernel_size; i++) {
      vec2 offset = in_texel_size * i;
      vec2 uv = in_uv + offset * multiplier;
      vec4 tex_color = texture(tex_input, uv);
      if (tex_color.a > 0.0) {
         out_color += tex_color;
         counter++;
      }
   }

   out_color /= float(counter);
}
