#pragma once

#include <cstdint>
#include <vector>
#include <nvmath/nvmath.h>
#include <context/context.h>
#include <shared/binding.h>
#include <shared/instance.h>
#include "alloc.h"
#include "mesh.h"

class Instance {
public:
  Instance(const nvmath::mat4f& tm, uint meshId) {
    m_transform = tm;
    m_meshIndex = meshId;
  }
  Instance(uint meshId) {
    m_meshIndex = meshId;
  }
  const mat4& getTransform() { return m_transform; }
  uint getMeshIndex() { return m_meshIndex; }

private:
  mat4 m_transform{nvmath::mat4f_id};
  uint m_meshIndex{0};      // Model index reference
};

class InstancesAlloc : public GpuAlloc {
public:
  InstancesAlloc(ContextAware* pContext, vector<Instance>& instances,
                 vector<MeshAlloc*>& meshAllocs,
                 const VkCommandBuffer& cmdBuf);
  void deinit(ContextAware* pContext);
  VkBuffer getBuffer() { return m_bInstances.buffer; }

private:
  vector<GpuInstance> m_instances{};
  nvvk::Buffer m_bInstances;
};