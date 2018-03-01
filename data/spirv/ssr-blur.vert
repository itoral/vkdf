#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform sampler2D tex_input;

layout(location = 0) out vec2 out_texel_size;
layout(location = 1) out vec2 out_uv;

void main()
{
   vec2 uv;
   switch (gl_VertexIndex) {
   case 0:
      gl_Position = vec4( 1.0,  1.0, 0.0, 1.0);
      out_uv = vec2(1.0, 1.0);
      break;
   case 1:
      gl_Position = vec4( 1.0, -1.0, 0.0, 1.0);
      out_uv = vec2(1.0, 0.0);
      break;
   case 2:
      gl_Position = vec4(-1.0,  1.0, 0.0, 1.0);
      out_uv = vec2(0.0, 1.0);
      break;
   case 3:
      gl_Position = vec4(-1.0, -1.0, 0.0, 1.0);
      out_uv = vec2(0.0, 0.0);
      break;
   }

   out_texel_size = 1.0 / textureSize(tex_input, 0);
}
