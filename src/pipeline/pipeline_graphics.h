#pragma once

#include <shared/camera.h>
#include <shared/pushconstant.h>
#include "pipeline.h"
#include <vector>

class PipelineGraphics : public PipelineAware {
public:
  enum class HoldSet {
    Scene = 0,
    Out = 1,
    Num = 2,
  };
  PipelineGraphics()
      : PipelineAware(uint(HoldSet::Num), RasterBindSet::RasterNum) {}
  virtual void init(ContextAware* pContext, Scene* pScene);
  virtual void run(const VkCommandBuffer& cmdBuf);
  virtual void deinit();
  void updateCameraBuffer(const VkCommandBuffer& cmdBuf);
  VkDescriptorImageInfo& getHdrOutImageInfo() {
    return m_tColors[0].descriptor;
  }
  DescriptorSetWrapper& getOutDescriptorSet() {
    return m_holdSetWrappers[uint(HoldSet::Out)];
  }
  DescriptorSetWrapper& getSceneDescriptorSet() {
    return m_holdSetWrappers[uint(HoldSet::Scene)];
  }
  nvvk::Texture& getColorTexture(uint textureId) {
    return m_tColors[textureId];
  }

private:
  vector<nvvk::Texture> m_tColors{};  // Canvas we draw things on
  nvvk::Texture m_tDepth;             // Depth buffer
  VkRenderPass m_offscreenRenderPass{VK_NULL_HANDLE};
  VkFramebuffer m_offscreenFramebuffer{VK_NULL_HANDLE};
  nvvk::Buffer m_bCamera;

private:
  void createOffscreenResources();  // Creating an offscreen frame buffer and
                                    // the associated render pass
  void createGraphicsDescriptorSetLayout();  // Describing the layout pushed
                                             // when rendering
  void createCameraBuffer();  // Creating the uniform buffer holding the camera
                              // matrices
  void updateGraphicsDescriptorSet();  // Setting up the buffers in the
                                       // descriptor set
};