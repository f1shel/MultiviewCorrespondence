#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : require
#extension GL_EXT_scalar_block_layout : require

#include "../shared/binding.h"
#include "../shared/camera.h"
#include "utils/structs.glsl"
#include "utils/math.glsl"

// clang-format off
layout(location = 0) rayPayloadInEXT RayPayload payload;
layout(set = RtScene, binding = SceneCamera)     uniform _Camera    { GpuCamera cameraInfo; };
// clang-format on

void main() {
  // Stop ray if it does not hit anything
  payload.pRec.stop = true;

  payload.pRec.radiance = vec3(0);
}