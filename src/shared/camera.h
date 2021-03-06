#ifndef CAMERA_H
#define CAMERA_H

#include "binding.h"

// clang-format off
START_ENUM(CameraType)
  CameraTypePerspective = 0,
  CameraTypeOpencv      = 1,
  CameraTypeUndefined   = 2
END_ENUM();
// clang-format on

// Uniform buffer set at each frame
struct GpuCamera {
  mat4 rasterToCamera;
  mat4 cameraToWorld;
  mat4 worldToCamera;
  mat4 worldToRaster;
  vec4 fxfycxcy;  // [focal_xy, center_xy], for opencv model
  vec3 padding;   // This is very important!!!
  uint type;      // camera type
};

struct GpuCameraPair {
  GpuCamera ref;
  GpuCamera src;
};

#endif