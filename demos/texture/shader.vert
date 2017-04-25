#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(std140, binding = 0) uniform ubo {
    mat4 mvp;
} UBO;

layout (location = 0) in vec4 pos;
layout (location = 1) in vec2 texCoord;

layout (location = 0) out vec2 outTexCoord;

out gl_PerVertex {
   vec4 gl_Position;
};

void main() {
   gl_Position = UBO.mvp * pos;
   outTexCoord = texCoord;
}
