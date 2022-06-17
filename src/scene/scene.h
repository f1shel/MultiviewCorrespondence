#pragma once

#include <shared/light.h>
#include <shared/instance.h>
#include <shared/sun_and_sky.h>
#include <shared/pushconstant.h>
#include <context/context.h>
#include <core/light.h>
#include <core/instance.h>
#include <core/integrator.h>
#include <core/state.h>
#include <core/material.h>
#include <core/mesh.h>
#include <core/texture.h>
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
  void addState(const State& piplineState);
  void addCamera(VkExtent2D filmResolution, float fov, float focalDist,
                 float aperture);                            // perspective
  void addCamera(VkExtent2D filmResolution, vec4 fxfycxcy);  // opencv
  void addLight(const GpuLight& light);
  void addLight(const GpuLight& light, const std::string& lightMeshPath);
  void addEnvMap(const std::string& envmapPath);
  void addTexture(const std::string& textureName,
                  const std::string& texturePath, float gamma);
  void addMaterial(const std::string& materialName,
                   const GpuMaterial& material);
  void addMesh(const std::string& meshName, const std::string& meshPath,
               bool recomputeNormal, vec2 uvScale);
  void addInstance(const nvmath::mat4f& transform, const std::string& meshName,
                   const std::string& materialName);
  void addShot(const CameraShot& shot);

public:
  int getMeshId(const std::string& meshName);
  int getTextureId(const std::string& textureName);
  int getMaterialId(const std::string& materialName);
  int getMeshesNum();
  int getInstancesNum();
  int getTexturesNum();
  int getMaterialsNum();
  int getLightsNum();
  int getShotsNum();
  CameraShot& getShot(int shotId);
  State& getPipelineState();
  void setSpp(int spp);
  Camera& getCamera();
  CameraType getCameraType();
  MaterialType getMaterialType(uint matId);
  nvvk::RaytracingBuilderKHR::BlasInput getBlas(VkDevice device, int meshId);
  vector<Instance>& getInstances();
  VkExtent2D getSize();
  VkBuffer getInstancesDescriptor();
  VkDescriptorImageInfo getTextureDescriptor(int textureId);
  vector<VkDescriptorImageInfo> getEnvMapDescriptor();
  VkBuffer getLightsDescriptor();
  VkBuffer getSunskyDescriptor();
  GpuSunAndSky& getSunsky();
  void setShot(int shotId);

private:
  using TextureTable = std::map<string, std::pair<Texture*, uint>>;
  using MeshTable = std::map<string, std::pair<Mesh*, uint>>;
  using MaterialTable = std::map<string, std::pair<Material*, uint>>;
  using MeshPropTable = std::map<uint32_t, std::pair<bool, uint32_t>>;
  // ---------------- ------------- ----------------
  std::string m_sceneFileDir = "";
  ContextAware* m_pContext = nullptr;
  bool m_hasScene = false;
  // ---------------- CPU resources ----------------
  // Integrator         m_integrator   = {};
  State m_pipelineState = {};
  Camera* m_pCamera = nullptr;
  EnvMap* m_pEnvMap = nullptr;
  vector<GpuLight> m_lights = {};
  TextureTable m_pTextures = {};
  MeshTable m_pMeshes = {};
  MaterialTable m_pMaterials = {};
  vector<Instance> m_instances = {};
  vector<CameraShot> m_shots = {};
  GpuSunAndSky m_sunAndSky = {};
  MeshPropTable m_mesh2light = {};
  // ---------------- GPU resources ----------------
  EnvMapAlloc* m_pEnvMapAlloc = nullptr;
  LightsAlloc* m_pLightsAlloc = nullptr;
  vector<TextureAlloc*> m_pTexturesAlloc = {};
  vector<MaterialAlloc*> m_pMaterialsAlloc = {};
  vector<MeshAlloc*> m_pMeshesAlloc = {};
  InstancesAlloc* m_pInstancesAlloc = nullptr;
  nvvk::Buffer m_bSunAndSky;
  // ---------------- ------------- ----------------
  Dimensions m_dimensions;

private:
  void allocLights(ContextAware* pContext, const VkCommandBuffer& cmdBuf);
  void allocTexture(ContextAware* pContext, uint32_t textureId,
                    const std::string& textureName, Texture* pTexture,
                    const VkCommandBuffer& cmdBuf);
  void allocMaterial(ContextAware* pContext, uint32_t materialId,
                     const std::string& materialName, Material* pMaterial,
                     const VkCommandBuffer& cmdBuf);
  void allocMesh(ContextAware* pContext, uint32_t meshId,
                 const std::string& meshName, Mesh* pMesh,
                 const VkCommandBuffer& cmdBuf);
  void allocInstances(ContextAware* pContext, const VkCommandBuffer& cmdBuf);
  void allocEnvMap(ContextAware* pContext, const VkCommandBuffer& cmdBuf);
  void allocSunAndSky(ContextAware* pContext, const VkCommandBuffer& cmdBuf);
  void computeSceneDimensions();
  void fitCamera();
};