#include "scene.h"

#include <nvmath/nvmath.h>
#include <nvh/fileoperations.hpp>
#include <nvh/timesampler.hpp>
#include <nvvk/buffers_vk.hpp>
#include <nvvk/commands_vk.hpp>

#include <filesystem>
#include <fstream>

void Scene::init(ContextAware* pContext) {
  m_pContext = pContext;
  reset();
}

void Scene::deinit() {
  if (m_pContext == nullptr) {
    LOG_ERROR("{}: failed to find belonging context when deinit.", "Scene");
    exit(1);
  }
  freeRawData();
  freeAllocData();
}

void Scene::submit() {
  LOG_INFO("{}: submitting resources to gpu", "Scene");

  auto& qGCT1 = m_pContext->getParallelQueues()[0];
  nvvk::CommandPool cmdBufGet(m_pContext->getDevice(), qGCT1.familyIndex,
                              VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                              qGCT1.queue);
  VkCommandBuffer cmdBuf = cmdBufGet.createCommandBuffer();

  m_pMeshesAlloc.resize(getMeshesNum());
  for (auto& record : m_pMeshes) {
    const auto& meshName = record.first;
    auto pMesh = record.second.first;
    auto meshId = record.second.second;
    allocMesh(m_pContext, meshId, meshName, pMesh, cmdBuf);
  }

  // Keeping the mesh description at host and device
  allocInstances(m_pContext, cmdBuf);

  cmdBufGet.submitAndWait(cmdBuf);
  m_pContext->getAlloc().finalizeAndReleaseStaging();

  // autofit
  if (m_shots.empty()) {
    computeSceneDimensions();
    fitCamera();
  }
  setShot(0);

  m_hasScene = true;
}

void Scene::reset() {
  if (m_hasScene) freeAllocData();
  freeRawData();
  m_hasScene = false;
}

void Scene::freeAllocData() {
  // free meshes alloc data
  for (auto& pMeshAlloc : m_pMeshesAlloc) {
    pMeshAlloc->deinit(m_pContext);
  }

  // free scene desc alloc data
  m_pInstancesAlloc->deinit(m_pContext);
  delete m_pInstancesAlloc;
  m_pInstancesAlloc = nullptr;

  // free sun and sky
  m_pContext->getAlloc().destroy(m_bSunAndSky);
}

void Scene::freeRawData() {
  delete m_pCamera;
  m_pCamera = nullptr;
  m_shots.clear();
  m_instances.clear();

  for (auto& record : m_pMeshes) {
    const auto& valuePair = record.second;
    auto pMesh = valuePair.first;
    delete pMesh;
  }
  m_pMeshes.clear();
}

void Scene::addCamera(VkExtent2D filmResolution, float fov, float focalDist,
                      float aperture) {
  if (m_pCamera) delete m_pCamera;
  m_pCamera = new CameraPerspective(filmResolution, fov, focalDist, aperture);
}

void Scene::addCamera(VkExtent2D filmResolution, vec4 fxfycxcy) {
  if (m_pCamera) delete m_pCamera;
  m_pCamera = new CameraOpencv(filmResolution, fxfycxcy);
}

void Scene::addMesh(const std::string& meshName, const std::string& meshPath,
                    bool recomputeNormal, vec2 uvScale) {
  Mesh* pMesh = new Mesh(meshPath, recomputeNormal, uvScale);
  m_pMeshes[meshName] = std::make_pair(pMesh, m_pMeshes.size());
}

void Scene::addInstance(const nvmath::mat4f& transform,
                        const std::string& meshName) {
  m_instances.emplace_back(Instance(transform, getMeshId(meshName)));
}

void Scene::addShot(const CameraShot& shot) { m_shots.emplace_back(shot); }

int Scene::getMeshId(const std::string& meshName) {
  if (m_pMeshes.count(meshName))
    return m_pMeshes[meshName].second;
  else {
    LOG_ERROR("{}: mesh [\"{}\"] does not exist\n", "Scene", meshName);
    exit(1);
  }
  return 0;
}

int Scene::getMeshesNum() { return m_pMeshes.size(); }

int Scene::getInstancesNum() { return m_instances.size(); }

int Scene::getShotsNum() { return m_shots.size(); }

CameraShot& Scene::getShot(int shotId) { return m_shots[shotId]; }

Camera& Scene::getCamera() { return *m_pCamera; }

CameraType Scene::getCameraType() { return m_pCamera->getType(); }

nvvk::RaytracingBuilderKHR::BlasInput Scene::getBlas(VkDevice device,
                                                     int meshId) {
  return MeshBufferToBlas(device, *m_pMeshesAlloc[meshId]);
}

vector<Instance>& Scene::getInstances() { return m_instances; }

VkExtent2D Scene::getSize() {
  return m_pContext->getSize();
  // return m_pCamera->getFilmSize();
}

VkBuffer Scene::getInstancesDescriptor() {
  return m_pInstancesAlloc->getBuffer();
}

VkBuffer Scene::getSunskyDescriptor() { return m_bSunAndSky.buffer; }

void Scene::setShot(int shotId) { m_pCamera->setToWorld(m_shots[shotId]); }

void Scene::allocMesh(ContextAware* pContext, uint32_t meshId,
                      const std::string& meshName, Mesh* pMesh,
                      const VkCommandBuffer& cmdBuf) {
  auto& m_debug = pContext->getDebug();
  auto m_device = pContext->getDevice();

  MeshAlloc* pMeshAlloc = new MeshAlloc(pContext, pMesh, cmdBuf);
  m_pMeshesAlloc[meshId] = pMeshAlloc;

  NAME2_VK(pMeshAlloc->getVerticesBuffer(),
           std::string(meshName + "_vertexBuffer"));
  NAME2_VK(pMeshAlloc->getIndicesBuffer(),
           std::string(meshName + "_indexBuffer"));
}

void Scene::allocInstances(ContextAware* pContext,
                           const VkCommandBuffer& cmdBuf) {
  // Keeping the obj host model and device description
  m_pInstancesAlloc =
      new InstancesAlloc(pContext, m_instances, m_pMeshesAlloc, cmdBuf);
}

void Scene::computeSceneDimensions() {
  Bbox scnBbox;

  for (auto& inst : m_instances) {
    auto pMeshAlloc = m_pMeshesAlloc[inst.getMeshIndex()];
    Bbox bbox(pMeshAlloc->getPosMin(), pMeshAlloc->getPosMax());
    bbox.transform(inst.getTransform());
    scnBbox.insert(bbox);
  }

  if (scnBbox.isEmpty() || !scnBbox.isVolume()) {
    LOG_WARN(
        "{}: scene bounding box invalid, Setting to: [-1,-1,-1], "
        "[1,1,1]",
        "Scene");
    scnBbox.insert({-1.0f, -1.0f, -1.0f});
    scnBbox.insert({1.0f, 1.0f, 1.0f});
  }

  m_dimensions.min = scnBbox.min();
  m_dimensions.max = scnBbox.max();
  m_dimensions.size = scnBbox.extents();
  m_dimensions.center = scnBbox.center();
  m_dimensions.radius = scnBbox.radius();
}

void Scene::fitCamera() {
  auto m_size = getSize();
  CameraManip.fit(m_dimensions.min, m_dimensions.max, true, false,
                  m_size.width / static_cast<float>(m_size.height));
  auto cam = CameraManip.getCamera();
  m_shots.emplace_back(
      CameraShot{cam.ctr, cam.eye, cam.up, nvmath::mat4f_zero});
}
