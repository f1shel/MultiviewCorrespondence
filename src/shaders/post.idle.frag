#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable
#extension GL_ARB_gpu_shader_int64 : enable  // Shader reference

layout(location = 0) in vec2 uvCoords;
layout(location = 0) out vec4 fragColor;

#include "../shared/binding.h"
#include "../shared/pushconstant.h"

layout(set = PostInput, binding = InputSampler) uniform sampler2D inImage;

void main() {
  // Raw result of ray tracing
  vec4 hdr = texture(inImage, uvCoords).rgba;
  fragColor.a = hdr.a;

  fragColor.rgb = hdr.rgb;
}
