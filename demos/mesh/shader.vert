#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(std140, set = 0, binding = 0) uniform vp_ubo {
    mat4 View;
    mat4 Projection;
} VP;

layout(std140, set = 0, binding = 1) uniform m_ubo {
     mat4 Model[501];
} M;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 out_normal;

void main()
{
   mat4 mvp = VP.Projection * VP.View * M.Model[gl_InstanceIndex];
   vec4 pos = vec4(in_position.x, in_position.y, in_position.z, 1.0);
   gl_Position = mvp * pos;
   out_normal = in_normal;
}
