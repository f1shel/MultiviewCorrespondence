#include "tracer.h"
#include <loader/loader.h>

#include <backends/imgui_impl_glfw.h>
#include <nvh/timesampler.hpp>
#include <nvvk/context_vk.hpp>
#include <nvvk/images_vk.hpp>
#include <nvvk/structs_vk.hpp>
#include <ext/tqdm.h>

#include <filesystem>
#include <iostream>

using std::filesystem::path;

void Tracer::init(TracerInitSettings tis) {
  m_tis = tis;

  // Get film size and set size for context
  auto filmResolution =
      Loader().loadSizeFirst(m_tis.scenefile, ContextAware::getRoot());
  ContextAware::setSize(filmResolution);

  // Initialize context and set context pointer for scene
  ContextAware::init({m_tis.offline});
  m_scene.init(reinterpret_cast<ContextAware*>(this));

  parallelLoading();
}

void Tracer::run() {
  if (ContextAware::getOfflineMode())
    runOffline();
  else
    runOnline();
}

void Tracer::deinit() {
  m_pipelineGraphics.deinit();
  m_pipelineRaytrace.deinit();
  m_scene.deinit();
  ContextAware::deinit();
}

void Tracer::runOnline() {
  std::array<VkClearValue, 2> clearValues{};
  clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
  clearValues[1].depthStencil = {1.0f, 0};

  // Main loop
  while (!shouldGlfwCloseWindow()) {
    glfwPollEvents();
    if (isMinimized()) continue;

    // Start the Dear ImGui frame
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Acquire swap chain
    prepareFrame();

    // Start command buffer of this frame
    uint32_t curFrame = getCurFrame();
    const VkCommandBuffer& cmdBuf = getCommandBuffers()[curFrame];
    VkCommandBufferBeginInfo beginInfo = nvvk::make<VkCommandBufferBeginInfo>();
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    {
      vkBeginCommandBuffer(cmdBuf, &beginInfo);

      // Update camera pairs
      m_pipelineGraphics.run(cmdBuf);

      // Ray tracing
      m_pipelineRaytrace.run(cmdBuf);

      // Post processing
      {
        VkRenderPassBeginInfo postRenderPassBeginInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        postRenderPassBeginInfo.clearValueCount = 2;
        postRenderPassBeginInfo.pClearValues = clearValues.data();
        postRenderPassBeginInfo.renderPass = getRenderPass();
        postRenderPassBeginInfo.framebuffer = getFramebuffer(curFrame);
        postRenderPassBeginInfo.renderArea = {{0, 0}, getSize()};

        // Rendering to the swapchain framebuffer the rendered image and
        // apply a tonemapper
        vkCmdBeginRenderPass(cmdBuf, &postRenderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        m_pipelinePost.run(cmdBuf);

        // Rendering UI
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);

        // Display axis in the lower left corner.
        // vkAxis.display(cmdBuf, CameraManip.getMatrix(),
        // vkSample.getSize());

        vkCmdEndRenderPass(cmdBuf);
      }
    }
    vkEndCommandBuffer(cmdBuf);
    submitFrame();
  }
  vkDeviceWaitIdle(getDevice());
}

void Tracer::runOffline() {
  std::array<VkClearValue, 2> clearValues{};
  clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
  clearValues[1].depthStencil = {1.0f, 0};

  // Vulkan allocator and image size
  auto& m_alloc = ContextAware::getAlloc();
  auto m_size = ContextAware::getSize();

  // Create a temporary buffer to hold the output pixels of the image
  VkBufferUsageFlags usage{VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  VkDeviceSize bufferSize = 4 * sizeof(float) * m_size.width * m_size.height;
  nvvk::Buffer pixelBuffer = m_alloc.createBuffer(
      bufferSize, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

  nvvk::CommandPool genCmdBuf(ContextAware::getDevice(),
                              ContextAware::getQueueFamily());

  auto pairsNum = m_scene.getPairsNum();
  
  tqdm bar;
  bar.set_theme_arrow();

  for (int pairId = 0; pairId < pairsNum; pairId++) {
    m_scene.setCurrentPair(pairId);
    bar.progress(pairId, pairsNum);

    const VkCommandBuffer& cmdBuf = genCmdBuf.createCommandBuffer();

    // Update camera and sunsky
    m_pipelineGraphics.run(cmdBuf);

    // Ray tracing and do not render gui
    m_pipelineRaytrace.run(cmdBuf);

    // Only post-processing in the last pass since
    // we do not care the intermediate result in offline mode
    VkRenderPassBeginInfo postRenderPassBeginInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    postRenderPassBeginInfo.clearValueCount = 2;
    postRenderPassBeginInfo.pClearValues = clearValues.data();
    postRenderPassBeginInfo.renderPass = ContextAware::getRenderPass();
    postRenderPassBeginInfo.framebuffer = ContextAware::getFramebuffer();
    postRenderPassBeginInfo.renderArea = {{0, 0}, ContextAware::getSize()};
    vkCmdBeginRenderPass(cmdBuf, &postRenderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    m_pipelinePost.run(cmdBuf);
    vkCmdEndRenderPass(cmdBuf);
    genCmdBuf.submitAndWait(cmdBuf);
    vkDeviceWaitIdle(ContextAware::getDevice());

    // Save image
    static char outputName[50];
    sprintf(outputName, "%s_pair_%04d.exr", m_tis.outputname.c_str(), pairId);
    saveBufferToImage(pixelBuffer, outputName, 0);
  }

  bar.finish();

  // Destroy temporary buffer
  m_alloc.destroy(pixelBuffer);
}

void Tracer::parallelLoading() {
  // Load resources into scene
  Loader().loadSceneFromJson(m_tis.scenefile, ContextAware::getRoot(),
                             &m_scene);

  // Create graphics pipeline
  m_pipelineGraphics.init(reinterpret_cast<ContextAware*>(this), &m_scene);

  // Raytrace pipeline use some resources from graphics pipeline
  PipelineRaytraceInitSetting pis;
  pis.pDswOut = &m_pipelineGraphics.getOutDescriptorSet();
  pis.pDswScene = &m_pipelineGraphics.getSceneDescriptorSet();
  m_pipelineRaytrace.init(reinterpret_cast<ContextAware*>(this), &m_scene, pis);

  // Post pipeline processes hdr output
  m_pipelinePost.init(reinterpret_cast<ContextAware*>(this), &m_scene,
                      &m_pipelineGraphics.getHdrOutImageInfo());
}

void Tracer::vkTextureToBuffer(const nvvk::Texture& imgIn,
                               const VkBuffer& pixelBufferOut) {
  nvvk::CommandPool genCmdBuf(ContextAware::getDevice(),
                              ContextAware::getQueueFamily());
  VkCommandBuffer cmdBuf = genCmdBuf.createCommandBuffer();

  // Make the image layout eTransferSrcOptimal to copy to buffer
  nvvk::cmdBarrierImageLayout(cmdBuf, imgIn.image, VK_IMAGE_LAYOUT_GENERAL,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_IMAGE_ASPECT_COLOR_BIT);

  // Copy the image to the buffer
  VkBufferImageCopy copyRegion;
  copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copyRegion.imageExtent = {ContextAware::getSize().width,
                            ContextAware::getSize().height, 1};
  copyRegion.imageOffset = {0};
  copyRegion.bufferOffset = 0;
  copyRegion.bufferImageHeight = ContextAware::getSize().height;
  copyRegion.bufferRowLength = ContextAware::getSize().width;
  vkCmdCopyImageToBuffer(cmdBuf, imgIn.image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pixelBufferOut,
                         1, &copyRegion);

  // Put back the image as it was
  nvvk::cmdBarrierImageLayout(
      cmdBuf, imgIn.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
  genCmdBuf.submitAndWait(cmdBuf);
}

void Tracer::saveBufferToImage(nvvk::Buffer pixelBuffer, std::string outputpath,
                               int channelId) {
  auto fp = path(outputpath);
  bool isRelativePath = fp.is_relative();
  if (isRelativePath) outputpath = NVPSystem::exePath() + outputpath;

  auto& m_alloc = ContextAware::getAlloc();
  auto m_size = ContextAware::getSize();

  // Default framebuffer color after post processing
  if (channelId == -1)
    vkTextureToBuffer(ContextAware::getOfflineColor(), pixelBuffer.buffer);
  // Hdr channel before post processing
  else
    vkTextureToBuffer(m_pipelineGraphics.getColorTexture(channelId),
                      pixelBuffer.buffer);

  // Write the image to disk
  void* data = m_alloc.map(pixelBuffer);
  writeImage(outputpath.c_str(), m_size.width, m_size.height,
             reinterpret_cast<float*>(data));
  m_alloc.unmap(pixelBuffer);
}
