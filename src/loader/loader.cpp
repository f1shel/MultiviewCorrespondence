#include "loader.h"
#include "utils.h"
#include <shared/camera.h>
#include <shared/pushconstant.h>
#include <filesystem/path.h>
using namespace filesystem;

#include <nvmath/nvmath.h>
#include <nvh/fileoperations.hpp>
#include <nvh/timesampler.hpp>
#include <nvvk/buffers_vk.hpp>
#include <nvvk/commands_vk.hpp>

#include <filesystem>
#include <fstream>

#define PI 3.14159265358979323846f

using nlohmann::json;
using std::ifstream;
using std::string;

static json defaultSceneOptions = json::parse(R"(
{
  "camera": {
    "fov": 45.0,
    "aperture": 0.0,
    "focal_distance": 0.1
  }
}
)");

VkExtent2D Loader::loadSizeFirst(std::string sceneFilePath,
                                 const std::string& root) {
  bool isRelativePath = !path(sceneFilePath).is_absolute();
  if (isRelativePath)
    sceneFilePath = nvh::findFile(sceneFilePath, {root}, true);
  if (sceneFilePath.empty()) {
    LOG_ERROR("{}: failed to load scene from file [{}]", "Loader",
              sceneFilePath);
    exit(1);
  }
  m_sceneFileDir = path(sceneFilePath).parent_path().str();

  ifstream sceneFileStream(sceneFilePath);
  json sceneFileJson;
  sceneFileStream >> sceneFileJson;

  // Multiview correspondence
  JsonCheckKeys(sceneFileJson, {"camera", "meshes", "instances", "shots", "pairs"});
  auto& cameraJson = sceneFileJson["camera"];
  JsonCheckKeys(cameraJson, {"type", "film"});
  auto& filmJson = cameraJson["film"];
  JsonCheckKeys(filmJson, {"resolution"});
  vec2 resolution = Json2Vec2(filmJson["resolution"]);
  VkExtent2D filmResolution = {uint(resolution.x), uint(resolution.y)};

  return filmResolution;
}

void Loader::loadSceneFromJson(std::string sceneFilePath,
                               const std::string& root, Scene* pScene) {
  LOG_INFO("{}: loading scene assets, this may take tens of seconds", "Loader");

  bool isRelativePath = !path(sceneFilePath).is_absolute();
  if (isRelativePath)
    sceneFilePath = nvh::findFile(sceneFilePath, {root}, true);
  if (sceneFilePath.empty()) {
    LOG_ERROR("{}: failed to load scene from file [{}]", "Loader",
              sceneFilePath);
    exit(1);
  }
  m_sceneFileDir = path(sceneFilePath).parent_path().str();

  ifstream sceneFileStream(sceneFilePath);
  json sceneFileJson;
  sceneFileStream >> sceneFileJson;

  m_pScene = pScene;
  m_pScene->reset();
  parse(sceneFileJson);
  submit();
}

void Loader::parse(const nlohmann::json& sceneFileJson) {
  JsonCheckKeys(sceneFileJson,
                // Multiview corresnpondence
                {"camera", "meshes", "instances", "shots", "pairs"});

  auto& cameraJson = sceneFileJson["camera"];
  auto& meshesJson = sceneFileJson["meshes"];
  auto& instancesJson = sceneFileJson["instances"];

  // parse scene file to generate raw data
  // camera
  addCamera(cameraJson);
  // meshes
  for (auto& meshJson : meshesJson) {
    addMesh(meshJson);
  }
  // instances
  for (auto& instanceJson : instancesJson) {
    addInstance(instanceJson);
  }
  // shots
  auto& shotsJson = sceneFileJson["shots"];
  for (auto& shotJson : shotsJson) {
    addShot(shotJson);
  }
  // Multiview corresnpondence
  // pairs
  auto& pairsJson = sceneFileJson["pairs"];
  for (auto& pairJson : pairsJson) {
    addPair(pairJson);
  }
}

void Loader::submit() { m_pScene->submit(); }

void Loader::addCamera(const nlohmann::json& cameraJson) {
  JsonCheckKeys(cameraJson, {"type", "film"});
  auto& filmJson = cameraJson["film"];
  JsonCheckKeys(filmJson, {"resolution"});
  vec2 resolution = Json2Vec2(filmJson["resolution"]);
  VkExtent2D filmResolution = {uint(resolution.x), uint(resolution.y)};
  nvmath::vec4f fxfycxcy = 0.0f;
  float fov = defaultSceneOptions["camera"]["fov"];
  float aperture = defaultSceneOptions["camera"]["aperture"];
  float focalDistance = defaultSceneOptions["camera"]["focal_distance"];
  if (cameraJson["type"] == "perspective") {
    if (cameraJson.contains("fov")) fov = cameraJson["fov"];
    if (cameraJson.contains("aperture")) aperture = cameraJson["aperture"];
    if (cameraJson.contains("focal_distance"))
      focalDistance = cameraJson["focal_distance"];
    m_pScene->addCamera(filmResolution, fov, focalDistance, aperture);
  } else if (cameraJson["type"] == "opencv") {
    JsonCheckKeys(cameraJson, {"fx", "fy", "cx", "cy"});
    fxfycxcy = {cameraJson["fx"], cameraJson["fy"], cameraJson["cx"],
                cameraJson["cy"]};
    m_pScene->addCamera(filmResolution, fxfycxcy);
  } else {
    LOG_ERROR("{}: unrecognized camera type [{}]", "Loader",
              cameraJson["type"]);
    exit(1);
  }
}

void Loader::addMesh(const nlohmann::json& meshJson) {
  JsonCheckKeys(meshJson, {"name", "path"});
  std::string meshName = meshJson["name"];
  auto meshPath = nvh::findFile(meshJson["path"], {m_sceneFileDir}, true);
  if (meshPath.empty()) {
    LOG_ERROR("{}: failed to load mesh from file [{}]", "Loader", meshPath);
    exit(1);
  }
  bool recomputeNormal = false;
  vec2 uvScale = {1.f, 1.f};
  if (meshJson.contains("recompute_normal"))
    recomputeNormal = meshJson["recompute_normal"];
  if (meshJson.contains("uv_scale")) uvScale = Json2Vec2(meshJson["uv_scale"]);

  m_pScene->addMesh(meshName, meshPath, recomputeNormal, uvScale);
}

static void parseToWorld(const json& toworldJson, mat4& transform,
                         bool banTranslation = false) {
  transform = nvmath::mat4f_id;
  for (const auto& singleton : toworldJson) {
    nvmath::mat4f t = nvmath::mat4f_id;
    JsonCheckKeys(singleton, {"type", "value"});
    if (singleton["type"] == "matrix") {
      t = Json2Mat4(singleton["value"]);
    } else if (singleton["type"] == "translate" && !banTranslation) {
      t.set_translation(Json2Vec3(singleton["value"]));
    } else if (singleton["type"] == "scale") {
      t.set_scale(Json2Vec3(singleton["value"]));
    } else if (singleton["type"] == "rotx") {
      t = nvmath::rotation_mat4_x(nv_to_rad * float(singleton["value"]));
    } else if (singleton["type"] == "roty") {
      t = nvmath::rotation_mat4_y(nv_to_rad * float(singleton["value"]));
    } else if (singleton["type"] == "rotz") {
      t = nvmath::rotation_mat4_z(nv_to_rad * float(singleton["value"]));
    } else if (singleton["type"] == "rotate") {
      vec3 xyz = Json2Vec3(singleton["value"]);
      t *= nvmath::rotation_mat4_z(nv_to_rad * xyz.z);
      t *= nvmath::rotation_mat4_y(nv_to_rad * xyz.y);
      t *= nvmath::rotation_mat4_x(nv_to_rad * xyz.x);
    } else {
      LOG_ERROR("{}: unrecognized toworld singleton type [{}]", "Loader",
                singleton["type"]);
      exit(1);
    }
    transform = t * transform;
  }
}

void Loader::addInstance(const nlohmann::json& instanceJson) {
  JsonCheckKeys(instanceJson, {"mesh"});
  string meshName = instanceJson["mesh"];
  nvmath::mat4f transform = nvmath::mat4f_id;
  if (instanceJson.contains("toworld"))
    parseToWorld(instanceJson["toworld"], transform);
  m_pScene->addInstance(transform, meshName);
}

void Loader::addShot(const nlohmann::json& shotJson) {
  JsonCheckKeys(shotJson, {"type"});
  vec3 eye, lookat, up;
  mat4 ext = nvmath::mat4f_zero;
  if (shotJson["type"] == "lookat") {
    JsonCheckKeys(shotJson, {"eye", "lookat", "up"});
    eye = Json2Vec3(shotJson["eye"]);
    lookat = Json2Vec3(shotJson["lookat"]);
    up = Json2Vec3(shotJson["up"]);
  } else if (shotJson["type"] == "opencv") {
    JsonCheckKeys(shotJson, {"matrix"});
    ext = Json2Mat4(shotJson["matrix"]);
    auto cameraToWorld = nvmath::invert_rot_trans(ext);
    cameraToWorld.get_translation(eye);
    up = vec3(cameraToWorld * vec4(0, -1, 0, 0));
    lookat = vec3(cameraToWorld * vec4(0, 0, 1, 1));
  } else {
    LOG_ERROR("{}: unrecognized shot type [{}]", "Loader", shotJson["type"]);
    exit(1);
  }

  CameraShot shot;
  //shot.ext = ext;
  shot.eye = eye;
  shot.up = up;
  shot.lookat = lookat;

  m_pScene->addShot(shot);
}

// Multiview corresnpondence
void Loader::addPair(const nlohmann::json& pairJson) {
  JsonCheckKeys(pairJson, {"ref", "src"});
  int ref = pairJson["ref"];
  int src = pairJson["src"];
  m_pScene->addPair(ref, src);
}
