#pragma once

#include <shared/instance.h>
#include <shared/pushconstant.h>
#include <context/context.h>
#include <core/instance.h>
#include <core/integrator.h>
#include <core/mesh.h>
#include <ext/json.hpp>

#include <map>
#include <string>
#include <vector>

class Scene {
public:
  void init(ContextAware* pContext);
  void deinit();
  void submit();
  void reset();
  void freeAllocData();
  void freeRawData();

public:
  void addCamera(VkExtent2D filmResolution, float fov, float focalDist,
                 float aperture);                            // perspective
  void addCamera(VkExtent2D filmResolution, vec4 fxfycxcy);  // opencv
  void addMesh(const std::string& meshName, const std::string& meshPath,
               bool recomputeNormal, vec2 uvScale);
  void addInstance(const nvmath::mat4f& transform, const std::string& meshName);
  void addShot(const CameraShot& shot);

public:
  int getMeshId(const std::string& meshName);
  int getMeshesNum();
  int getInstancesNum();
  int getShotsNum();
  CameraShot& getShot(int shotId);
  Camera& getCamera();
  CameraType getCameraType();
  nvvk::RaytracingBuilderKHR::BlasInput getBlas(VkDevice device, int meshId);
  vector<Instance>& getInstances();
  VkExtent2D getSize();
  VkBuffer getInstancesDescriptor();
  VkBuffer getSunskyDescriptor();
  void setShot(int shotId);

private:
  using MeshTable = std::map<string, std::pair<Mesh*, uint>>;
  using MeshPropTable = std::map<uint32_t, std::pair<bool, uint32_t>>;
  // ---------------- ------------- ----------------
  std::string m_sceneFileDir = "";
  ContextAware* m_pContext = nullptr;
  bool m_hasScene = false;
  // ---------------- CPU resources ----------------
  // Integrator         m_integrator   = {};
  Camera* m_pCamera = nullptr;
  MeshTable m_pMeshes = {};
  vector<Instance> m_instances = {};
  vector<CameraShot> m_shots = {};
  MeshPropTable m_mesh2light = {};
  // ---------------- GPU resources ----------------
  vector<MeshAlloc*> m_pMeshesAlloc = {};
  InstancesAlloc* m_pInstancesAlloc = nullptr;
  nvvk::Buffer m_bSunAndSky;
  // ---------------- ------------- ----------------
  Dimensions m_dimensions;

private:
  void allocMesh(ContextAware* pContext, uint32_t meshId,
                 const std::string& meshName, Mesh* pMesh,
                 const VkCommandBuffer& cmdBuf);
  void allocInstances(ContextAware* pContext, const VkCommandBuffer& cmdBuf);
  void computeSceneDimensions();
  void fitCamera();
};