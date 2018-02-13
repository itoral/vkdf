#version 450

#extension GL_ARB_separate_shader_objects : enable

/**
 * Does a simple blur pass over the input SSAO texture. To prevent the "halo"
 * effect at the edges of geometry caused by blurring together pixels with
 * and without occlusion. we only blur together pixels that have "similar"
 * ssao values, meaning that the difference between their values is lower
 * than a threshold.
 */
layout(push_constant) uniform pcb {
   int blur_size;
   float threshold;
} PCB;

layout(set = 0, binding = 0) uniform sampler2D tex_ssao;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out float out_blur_ssao;

void main()
{
   vec2 texel_size = 1.0 / vec2(textureSize(tex_ssao, 0));

   float ref = texture(tex_ssao, in_uv).r;
   float result = ref;
   int sample_count = 1;

   for (int x = -PCB.blur_size; x < PCB.blur_size; ++x)
   {
      for (int y = -PCB.blur_size; y < PCB.blur_size; ++y)
      {
         if (x != 0 || y != 0) {
            vec2 tex_offset = in_uv + vec2(x, y) * texel_size;
            tex_offset = min(vec2(1.0), max(vec2(0.0), tex_offset));
            float value = texture(tex_ssao, tex_offset).r;
            if (abs(value - ref) <= PCB.threshold) {
               result += value;
               sample_count++;
            }
         }
      }
   }

   out_blur_ssao = result / float(sample_count);
}
