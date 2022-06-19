#include "pipeline_raytrace.h"
#include <nvh/fileoperations.hpp>
#include <nvh/timesampler.hpp>
#include <nvvk/buffers_vk.hpp>
#include "nvvk/shaders_vk.hpp"

void PipelineRaytrace::init(ContextAware* pContext, Scene* pScene,
                            PipelineRaytraceInitSetting& pis) {
  LOG_INFO("{}: creating raytrace pipeline", "Pipeline");
  m_pContext = pContext;
  m_pScene = pScene;
  // Ray tracing
  initRayTracing();
  createBottomLevelAS();
  createTopLevelAS();
  createRtDescriptorSetLayout();
  bind(RtBindSet::RtAccel, &m_holdSetWrappers[uint(HoldSet::Accel)]);
  bind(RtBindSet::RtOut, pis.pDswOut);
  bind(RtBindSet::RtScene, pis.pDswScene);
  createRtPipeline();
  updateRtDescriptorSet();
}

void PipelineRaytrace::deinit() {
  m_blas.clear();
  m_tlas.clear();
  // m_pushconstant = {0};

  m_rtBuilder.destroy();
  m_sbt.destroy();

  PipelineAware::deinit();
}

void PipelineRaytrace::run(const VkCommandBuffer& cmdBuf) {
  // If the camera matrix has changed, resets the frame; otherwise, increments
  // frame.
  static nvmath::mat4f refCamMatrix{0};
  static float refFov{CameraManip.getFov()};

  const auto& m = CameraManip.getMatrix();
  const auto fov = CameraManip.getFov();

  if (memcmp(&refCamMatrix.a00, &m.a00, sizeof(nvmath::mat4f)) != 0 ||
      refFov != fov) {
    refCamMatrix = m;
    refFov = fov;
  }

  // Do ray tracing
  vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);
  vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                          m_pipelineLayout, 0, (uint32_t)m_bindSets.size(),
                          m_bindSets.data(), 0, nullptr);
  static GpuPushConstantRaytrace rtsState = {};
  vkCmdPushConstants(cmdBuf, m_pipelineLayout, VK_SHADER_STAGE_ALL, 0,
                     sizeof(GpuPushConstantRaytrace), &rtsState);

  const auto& regions = m_sbt.getRegions();
  const auto size = m_pContext->getSize();

  // Run the ray tracing pipeline and trace rays
  vkCmdTraceRaysKHR(cmdBuf,       // Command buffer
                    &regions[0],  // Region of memory with ray generation groups
                    &regions[1],  // Region of memory with miss groups
                    &regions[2],  // Region of memory with hit groups
                    &regions[3],  // Region of memory with callable groups
                    size.width,   // Width of dispatch
                    size.height,  // Height of dispatch
                    1);           // Depth of dispatch
}

void PipelineRaytrace::initRayTracing() {
  auto& m_alloc = m_pContext->getAlloc();
  auto m_device = m_pContext->getDevice();
  auto m_physicalDevice = m_pContext->getPhysicalDevice();

  VkPhysicalDeviceRayTracingPipelinePropertiesKHR prop = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};

  // Requesting ray tracing properties
  VkPhysicalDeviceProperties2 prop2 = nvvk::make<VkPhysicalDeviceProperties2>();
  prop2.pNext = &prop;
  vkGetPhysicalDeviceProperties2(m_physicalDevice, &prop2);

  auto& qC = m_pContext->getParallelQueues()[1];
  m_rtBuilder.setup(m_device, &m_alloc, qC.familyIndex);
  auto& qT = m_pContext->getParallelQueues()[2];
  m_sbt.setup(m_device, qT.familyIndex, &m_alloc, prop);
}

void PipelineRaytrace::createBottomLevelAS() {
  auto m_device = m_pContext->getDevice();
  // BLAS - Storing each primitive in a geometry
  m_blas.reserve(m_pScene->getMeshesNum());
  for (uint32_t meshId = 0; meshId < m_pScene->getMeshesNum(); meshId++) {
    // We could add more geometry in each BLAS, but we add only one for now
    m_blas.push_back(m_pScene->getBlas(m_device, meshId));
  }
  m_rtBuilder.buildBlas(
      m_blas, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
                  VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR);
}

void PipelineRaytrace::createTopLevelAS() {
  m_tlas.reserve(m_pScene->getInstancesNum());
  auto& instances = m_pScene->getInstances();
  for (uint32_t instId = 0; instId < m_pScene->getInstancesNum(); instId++) {
    auto& inst = instances[instId];
    VkAccelerationStructureInstanceKHR rayInst{};
    rayInst.transform = nvvk::toTransformMatrixKHR(inst.getTransform());
    rayInst.instanceCustomIndex = 0;  // gl_InstanceCustomIndexEXT
    rayInst.accelerationStructureReference =
        m_rtBuilder.getBlasDeviceAddress(inst.getMeshIndex());
    rayInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    rayInst.mask = 0xFF;  // Only be hit if rayMask & instance.mask != 0
    rayInst.instanceShaderBindingTableRecordOffset = 0;
    m_tlas.emplace_back(rayInst);
  }

  m_rtBuilder.buildTlas(
      m_tlas, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                  VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
}

void PipelineRaytrace::createRtDescriptorSetLayout() {
  auto m_device = m_pContext->getDevice();
  auto& accelDsw = m_holdSetWrappers[uint(HoldSet::Accel)];
  auto& bind = accelDsw.getDescriptorSetBindings();
  auto& layout = accelDsw.getDescriptorSetLayout();
  auto& set = accelDsw.getDescriptorSet();
  auto& pool = accelDsw.getDescriptorPool();

  // Create Binding Set
  bind.addBinding(AccelBindings::AccelTlas,
                  VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
                  VK_SHADER_STAGE_ALL);
  pool = bind.createPool(m_device);
  layout = bind.createLayout(m_device);
  set = nvvk::allocateDescriptorSet(m_device, pool, layout);
}

void PipelineRaytrace::createRtPipeline() {
  auto& m_alloc = m_pContext->getAlloc();
  auto& m_debug = m_pContext->getDebug();
  auto m_device = m_pContext->getDevice();

  // Creating all shaders
  enum StageIndices { RayGen, RayMiss, ShadowMiss, NumStages };
  array<VkPipelineShaderStageCreateInfo, NumStages + 1> stages{};
  // Raygen
  auto root = m_pContext->getRoot();
  auto stage = nvvk::make<VkPipelineShaderStageCreateInfo>();
  stage.pName = "main";  // All the same entry point
  stage.module = nvvk::createShaderModule(
      m_device, nvh::loadFile("../shaders/raytrace.correspondence.rgen.spv",
                              true, {root}));
  stage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
  stages[RayGen] = stage;
  NAME2_VK(stage.module, "RayGen");
  // Miss
  stage.module = nvvk::createShaderModule(
      m_device,
      nvh::loadFile("../shaders/raytrace.default.rmiss.spv", true, {root}));
  stage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
  stages[RayMiss] = stage;
  NAME2_VK(stage.module, "RayMiss");
  // Shadow miss
  stage.module = nvvk::createShaderModule(
      m_device,
      nvh::loadFile("../shaders/raytrace.shadow.rmiss.spv", true, {root}));
  stage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
  stages[ShadowMiss] = stage;
  NAME2_VK(stage.module, "Shadowmiss");
  // ClosetHit:BrdfLambertian
  stage.module = nvvk::createShaderModule(
      m_device,
      nvh::loadFile("../shaders/raytrace.intersect.rchit.spv", true, {root}));
  stage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
  stages[NumStages + 0] = stage;
  NAME2_VK(stage.module, "ClosetHit:BrdfLambertian");
  // Shader groups
  VkRayTracingShaderGroupCreateInfoKHR group =
      nvvk::make<VkRayTracingShaderGroupCreateInfoKHR>();
  std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups{};

  group.anyHitShader = VK_SHADER_UNUSED_KHR;
  group.closestHitShader = VK_SHADER_UNUSED_KHR;
  group.intersectionShader = VK_SHADER_UNUSED_KHR;
  group.generalShader = VK_SHADER_UNUSED_KHR;

  // Raygen
  group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
  group.generalShader = RayGen;
  shaderGroups.push_back(group);

  // Miss
  group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
  group.generalShader = RayMiss;
  shaderGroups.push_back(group);

  // shadow miss
  group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
  group.generalShader = ShadowMiss;
  shaderGroups.push_back(group);

  // closest hit shader
  group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
  group.generalShader = VK_SHADER_UNUSED_KHR;
  for (uint materialTypeId = 0; materialTypeId < 1; materialTypeId++) {
    group.closestHitShader = NumStages + materialTypeId;
    shaderGroups.push_back(group);
  }

  // Push constant: we want to be able to update constants used by the shaders
  VkPushConstantRange pushConstant{VK_SHADER_STAGE_ALL, 0,
                                   sizeof(GpuPushConstantRaytrace)};

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
  pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstant;

  array<VkDescriptorSetLayout, RtBindSet::RtNum> rtDescSetLayouts{};
  for (uint setId = 0; setId < RtBindSet::RtNum; setId++)
    rtDescSetLayouts[setId] =
        m_bindSetWrappers[setId]->getDescriptorSetLayout();
  pipelineLayoutCreateInfo.setLayoutCount =
      static_cast<uint32_t>(rtDescSetLayouts.size());
  pipelineLayoutCreateInfo.pSetLayouts = rtDescSetLayouts.data();
  vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, nullptr,
                         &m_pipelineLayout);

  // Assemble the shader stages and recursion depth info into the ray tracing
  // pipeline
  VkRayTracingPipelineCreateInfoKHR rayPipelineInfo{
      VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
  rayPipelineInfo.stageCount =
      static_cast<uint32_t>(stages.size());  // Stages are shaders
  rayPipelineInfo.pStages = stages.data();
  rayPipelineInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
  rayPipelineInfo.pGroups = shaderGroups.data();
  rayPipelineInfo.maxPipelineRayRecursionDepth = 4;  // Ray depth
  rayPipelineInfo.layout = m_pipelineLayout;
  vkCreateRayTracingPipelinesKHR(m_device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
                                 &rayPipelineInfo, nullptr, &m_pipeline);

  // Creating the SBT
  m_sbt.create(m_pipeline, rayPipelineInfo);

  // Removing temp modules
  for (auto& s : stages) vkDestroyShaderModule(m_device, s.module, nullptr);
}

void PipelineRaytrace::updateRtDescriptorSet() {
  auto m_device = m_pContext->getDevice();

  auto& accelDsw = m_holdSetWrappers[uint(HoldSet::Accel)];
  auto& bind = accelDsw.getDescriptorSetBindings();
  auto& layout = accelDsw.getDescriptorSetLayout();
  auto& set = accelDsw.getDescriptorSet();
  auto& pool = accelDsw.getDescriptorPool();

  std::vector<VkWriteDescriptorSet> writes;

  // Write to descriptors
  VkAccelerationStructureKHR tlas = m_rtBuilder.getAccelerationStructure();
  VkWriteDescriptorSetAccelerationStructureKHR descASInfo{
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  descASInfo.accelerationStructureCount = 1;
  descASInfo.pAccelerationStructures = &tlas;
  writes.emplace_back(
      bind.makeWrite(set, AccelBindings::AccelTlas, &descASInfo));
  vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()),
                         writes.data(), 0, nullptr);
}
