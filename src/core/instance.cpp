#include "instance.h"
#include <nvvk/buffers_vk.hpp>

InstancesAlloc::InstancesAlloc(ContextAware* pContext,
                               vector<Instance>& instances,
                               vector<MeshAlloc*>& meshAllocs,
                               const VkCommandBuffer& cmdBuf) {
  auto& m_alloc = pContext->getAlloc();
  auto m_device = pContext->getDevice();
  for (auto& instance : instances) {
    GpuInstance desc;
    auto pMeshAlloc = meshAllocs[instance.getMeshIndex()];
    desc.vertexAddress =
        nvvk::getBufferDeviceAddress(m_device, pMeshAlloc->getVerticesBuffer());
    desc.indexAddress =
        nvvk::getBufferDeviceAddress(m_device, pMeshAlloc->getIndicesBuffer());
    m_instances.emplace_back(desc);
  }
  m_bInstances = m_alloc.createBuffer(cmdBuf, m_instances,
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void InstancesAlloc::deinit(ContextAware* pContext) {
  pContext->getAlloc().destroy(m_bInstances);
  intoReleased();
}
