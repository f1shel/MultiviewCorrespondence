#ifndef STRUCTS_GLSL
#define STRUCTS_GLSL

#define USE_MIS 1

struct Ray {
  // Origin in world space
  vec3 o;
  // Direction in world space
  vec3 d;
};

struct RayPayload {
  Ray r;
  vec3 hitPos;
  vec3 ffnormal;
  bool hitSomething;
};

#endif