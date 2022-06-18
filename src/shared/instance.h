#ifndef INSTANCE_H
#define INSTANCE_H

#include "binding.h"

// Information of a obj model when referenced in a shader
struct GpuInstance {
  // Address of the Vertex buffer
  uint64_t vertexAddress;
  // Address of the index buffer
  uint64_t indexAddress;
};

// SceneDesc = GPUMeshDesc[] + GPUMaterialDesc[]

#endif