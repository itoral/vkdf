#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform pcb {
   int blur_size;
} PCB;

layout(set = 0, binding = 0) uniform sampler2D tex_ssao;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out float out_blur_ssao;

void main()
{
   vec2 texel_size = 1.0 / vec2(textureSize(tex_ssao, 0));

   float result = 0.0;
   for (int x = -PCB.blur_size; x < PCB.blur_size; ++x)
   {
      for (int y = -PCB.blur_size; y < PCB.blur_size; ++y)
      {
         vec2 tex_offset = in_uv + vec2(x, y) * texel_size;
         tex_offset = min(vec2(1.0), max(vec2(0.0), tex_offset));
         result += texture(tex_ssao, tex_offset).r;
      }
   }

   out_blur_ssao = result / (4 * PCB.blur_size * PCB.blur_size);
}
