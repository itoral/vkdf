#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform pcb {
   mat4 Proj;
   float aspect_ratio;
   float tan_half_fov;
} PCB;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_view_ray;

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

   out_view_ray.x = -gl_Position.x * PCB.aspect_ratio * PCB.tan_half_fov;
   out_view_ray.y = gl_Position.y * PCB.tan_half_fov;
}
