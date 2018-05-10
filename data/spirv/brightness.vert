#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(std140, set = 0, binding = 0) uniform brightness_ubo
{
   float brightness;
} B;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out flat float out_brightness;

void main()
{
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

   out_brightness = B.brightness;
}
