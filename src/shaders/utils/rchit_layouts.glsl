#ifndef LAYOUTS_GLSL
#define LAYOUTS_GLSL

#include "../../shared/binding.h"
#include "../../shared/pushconstant.h"
#include "../../shared/instance.h"
#include "../../shared/vertex.h"
#include "../../shared/camera.h"
#include "math.glsl"
#include "structs.glsl"

// clang-format off
layout(buffer_reference, scalar) buffer Vertices  { GpuVertex v[];   };
layout(buffer_reference, scalar) buffer Indices   { ivec3 i[];       };
//
layout(set = RtAccel, binding = AccelTlas)              uniform accelerationStructureEXT tlas;
layout(set = RtScene, binding = SceneInstances, scalar) buffer  _Instances { GpuInstance i[];        } instances;
layout(set = RtScene, binding = SceneCamera)            uniform _Camera    { GpuCamera cameraInfo; };
//
layout(push_constant)                                   uniform _RtxState  { GpuPushConstantRaytrace pc; };
//
layout(location = 0) rayPayloadInEXT RayPayload payload;
//
hitAttributeEXT vec2 _bary;
// clang-format on

struct HitState {
  // hit point position
  vec3 pos;
  // normalized view direction in world space
  vec3 V;
  // interpolated vertex normal/shading Normal
  vec3 N;
  // geo normal
  vec3 geoN;
  // face forward normal
  vec3 ffN;
};

// clang-format off
void configureShadingFrame(inout HitState state) {
  if (pc.useFaceNormal == 1) state.N = state.geoN;
  state.ffN = dot(state.N, state.V) > 0 ? state.N : -state.N;
}

HitState getHitState() {
  HitState state;

  GpuInstance _inst     = instances.i[gl_InstanceID];
  Indices     _indices  = Indices(_inst.indexAddress);
  Vertices    _vertices = Vertices(_inst.vertexAddress);

  ivec3     id = _indices.i[gl_PrimitiveID];
  GpuVertex v0 = _vertices.v[id.x];
  GpuVertex v1 = _vertices.v[id.y];
  GpuVertex v2 = _vertices.v[id.z];
  vec3      ba = vec3(1.0 - _bary.x - _bary.y, _bary.x, _bary.y);

  state.pos     = gl_ObjectToWorldEXT * vec4(barymix3(v0.pos, v1.pos, v2.pos, ba), 1.f);
  state.N       = barymix3(v0.normal, v1.normal, v2.normal, ba);
  state.N       = makeNormal((state.N * gl_WorldToObjectEXT).xyz);
  state.ffN     = cross(v1.pos - v0.pos, v2.pos - v0.pos);
  state.ffN     = makeNormal((state.ffN * gl_WorldToObjectEXT).xyz);
  state.V       = makeNormal(-gl_WorldRayDirectionEXT);

  configureShadingFrame(state);

  return state;
}
// clang-format on

#endif