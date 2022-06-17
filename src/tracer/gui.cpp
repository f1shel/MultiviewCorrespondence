#include "tracer.h"

#include <imgui_helper.h>
#include <imgui_orient.h>
#include <bitset>  // std::bitset

using GuiH = ImGuiH::Control;

void Tracer::renderGUI() {
  static bool showGui = true;

  if (m_busy) {
    guiBusy();
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_H)) showGui = !showGui;
  if (!showGui) return;

  // Show UI panel window.
  float panelAlpha = 1.0f;
  ImGuiH::Control::style.ctrlPerc = 0.55f;
  ImGuiH::Panel::Begin(ImGuiH::Panel::Side::Right, panelAlpha);

  bool changed{false};

  if (ImGui::CollapsingHeader("Camera" /*, ImGuiTreeNodeFlags_DefaultOpen*/))
    changed |= guiCamera();
  if (ImGui::CollapsingHeader(
          "Environment" /*, ImGuiTreeNodeFlags_DefaultOpen*/))
    changed |= guiEnvironment();
  if (ImGui::CollapsingHeader(
          "PathTracer" /*, ImGuiTreeNodeFlags_DefaultOpen*/))
    changed |= guiPathTracer();
  if (ImGui::CollapsingHeader("Denoiser" /*, ImGuiTreeNodeFlags_DefaultOpen*/))
    changed |= guiDenoiser();
  if (ImGui::CollapsingHeader(
          "Tonemapper" /*, ImGuiTreeNodeFlags_DefaultOpen*/))
    changed |= guiTonemapper();

  ImGui::End();  // ImGui::Panel::end()

  if (changed) {
    m_pipelineRaytrace.resetFrame();
  }
}

bool Tracer::guiCamera() {
  static GpuCamera dc{
      nvmath::mat4f_zero,  // rasterToCamera
      nvmath::mat4f_zero,  // cameraToWorld
      nvmath::mat4f_id,
      vec4(0.f, 0.f, 0.f, 0.f),  // fxfycxcy
      CameraTypeUndefined,       // type
      0.f,                       // aperture
      0.1f,                      // focal distance
  };

  bool changed{false};
  changed |= ImGuiH::CameraWidget();
  auto camType = m_scene.getCameraType();
  if (camType == CameraTypePerspective) {
    auto pCamera = static_cast<CameraPerspective*>(&m_scene.getCamera());
    changed |= GuiH::Group<bool>("Perspective", true, [&] {
      changed |=
          GuiH::Slider("Focal distance", "", &pCamera->getFocalDistance(),
                       &dc.focalDistance, GuiH::Flags::Normal, 0.01f, 10.f);
      changed |= GuiH::Slider("Aperture", "", &pCamera->getAperture(),
                              &dc.aperture, GuiH::Flags::Normal, 0.f, 1.f);
      return changed;
    });
  }
  return changed;
}

bool Tracer::guiEnvironment() {
  static GpuSunAndSky dss{
      {1, 1, 1},            // rgb_unit_conversion;
      0.0000101320f,        // multiplier;
      0.0f,                 // haze;
      0.0f,                 // redblueshift;
      1.0f,                 // saturation;
      0.0f,                 // horizon_height;
      {0.4f, 0.4f, 0.4f},   // ground_color;
      0.1f,                 // horizon_blur;
      {0.0, 0.0, 0.01f},    // night_color;
      0.8f,                 // sun_disk_intensity;
      {0.00, 0.78, 0.62f},  // sun_direction;
      5.0f,                 // sun_disk_scale;
      1.0f,                 // sun_glow_intensity;
      1,                    // y_is_up;
      1,                    // physically_scaled_sun;
      0,                    // in_use;
  };

  bool changed{false};
  auto& sunAndSky(m_scene.getSunsky());
  auto& rtxState = m_pipelineRaytrace.getPushconstant();

  static float rotx{0.f}, roty{0.f}, rotz{0.f};
  bool rotChanged = false;
  rotChanged |= GuiH::Slider("Rotate Envmap X", "", &rotx, nullptr,
                             GuiH::Flags::Normal, 0.f, 360.f);
  rotChanged |= GuiH::Slider("Rotate Envmap Y", "", &roty, nullptr,
                             GuiH::Flags::Normal, 0.f, 360.f);
  rotChanged |= GuiH::Slider("Rotate Envmap Z", "", &rotz, nullptr,
                             GuiH::Flags::Normal, 0.f, 360.f);
  if (rotChanged) {
    mat4 envR = nvmath::rotation_mat4_z(nv_to_rad * rotz) *
                nvmath::rotation_mat4_y(nv_to_rad * roty) *
                nvmath::rotation_mat4_x(nv_to_rad * rotx);
    m_scene.getCamera().setEnvRotate(envR);
  }
  changed |= rotChanged;
  changed |= GuiH::Slider("Envmap Intensity", "", &rtxState.envMapIntensity,
                          nullptr, GuiH::Flags::Normal, 0.f, 20.f);
  changed |= ImGui::Checkbox("Use Sun & Sky", (bool*)&sunAndSky.in_use);

  // Adjusting the up with the camera
  nvmath::vec3f eye, center, up;
  CameraManip.getLookat(eye, center, up);
  sunAndSky.y_is_up = (up.y == 1);

  if (sunAndSky.in_use) {
    GuiH::Group<bool>("Sun", true, [&] {
      changed |= GuiH::Custom("Direction", "Sun Direction", [&] {
        float indent = ImGui::GetCursorPos().x;
        changed |= ImGui::DirectionGizmo("", &sunAndSky.sun_direction.x, true);
        ImGui::NewLine();
        ImGui::SameLine(indent);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        changed |= ImGui::InputFloat3("##IG", &sunAndSky.sun_direction.x);
        return changed;
      });
      changed |=
          GuiH::Slider("Disk Scale", "", &sunAndSky.sun_disk_scale,
                       &dss.sun_disk_scale, GuiH::Flags::Normal, 0.f, 100.f);
      changed |=
          GuiH::Slider("Glow Intensity", "", &sunAndSky.sun_glow_intensity,
                       &dss.sun_glow_intensity, GuiH::Flags::Normal, 0.f, 5.f);
      changed |=
          GuiH::Slider("Disk Intensity", "", &sunAndSky.sun_disk_intensity,
                       &dss.sun_disk_intensity, GuiH::Flags::Normal, 0.f, 5.f);
      changed |= GuiH::Color("Night Color", "", &sunAndSky.night_color.x,
                             &dss.night_color.x, GuiH::Flags::Normal);
      return changed;
    });

    GuiH::Group<bool>("Ground", true, [&] {
      changed |=
          GuiH::Slider("Horizon Height", "", &sunAndSky.horizon_height,
                       &dss.horizon_height, GuiH::Flags::Normal, -1.f, 1.f);
      changed |= GuiH::Slider("Horizon Blur", "", &sunAndSky.horizon_blur,
                              &dss.horizon_blur, GuiH::Flags::Normal, 0.f, 1.f);
      changed |= GuiH::Color("Ground Color", "", &sunAndSky.ground_color.x,
                             &dss.ground_color.x, GuiH::Flags::Normal);
      changed |= GuiH::Slider("Haze", "", &sunAndSky.haze, &dss.haze,
                              GuiH::Flags::Normal, 0.f, 15.f);
      return changed;
    });

    GuiH::Group<bool>("Other", false, [&] {
      changed |= GuiH::Drag("Multiplier", "", &sunAndSky.multiplier,
                            &dss.multiplier, GuiH::Flags::Normal, 0.f,
                            std::numeric_limits<float>::max(), 2, "%5.5f");
      changed |= GuiH::Slider("Saturation", "", &sunAndSky.saturation,
                              &dss.saturation, GuiH::Flags::Normal, 0.f, 1.f);
      changed |=
          GuiH::Slider("Red Blue Shift", "", &sunAndSky.redblueshift,
                       &dss.redblueshift, GuiH::Flags::Normal, -1.f, 1.f);
      changed |=
          GuiH::Color("RGB Conversion", "", &sunAndSky.rgb_unit_conversion.x,
                      &dss.rgb_unit_conversion.x, GuiH::Flags::Normal);

      nvmath::vec3f eye, center, up;
      CameraManip.getLookat(eye, center, up);
      sunAndSky.y_is_up = up.y == 1;
      changed |= GuiH::Checkbox("Y is Up", "", (bool*)&sunAndSky.y_is_up,
                                nullptr, GuiH::Flags::Disabled);
      return changed;
    });
  }

  return changed;
}

bool Tracer::guiTonemapper() {
  static GpuPushConstantPost default_tm{
      1.0f,          // brightness;
      1.0f,          // contrast;
      1.0f,          // saturation;
      0.0f,          // vignette;
      1.0f,          // avgLum;
      1.0f,          // zoom;
      {1.0f, 1.0f},  // renderingRatio;
      0,             // autoExposure;
      0.5f,          // Ywhite;  // Burning white
      0.5f,          // key;     // Log-average luminance
      0,             // toneMappingType
  };
  static vector<const char*> ToneMappingTypeList = {
      "None", "Gamma", "Reinhard", "Aces", "Filmic", "Pbrt", "Custom"};

  auto& tm = m_pipelinePost.getPushconstant();
  bool changed{false};

  changed |=
      ImGui::Combo("Tone Mapping", (int*)&tm.tmType, ToneMappingTypeList.data(),
                   ToneMappingTypeList.size());

  if (tm.tmType == ToneMappingTypeCustom) {
    std::bitset<8> b(tm.autoExposure);
    bool autoExposure = b.test(0);
    changed |= GuiH::Checkbox("Auto Exposure", "Adjust exposure",
                              (bool*)&autoExposure);
    changed |=
        GuiH::Slider("Exposure", "Scene Exposure", &tm.avgLum,
                     &default_tm.avgLum, GuiH::Flags::Normal, 0.001f, 5.00f);
    changed |=
        GuiH::Slider("Brightness", "", &tm.brightness, &default_tm.brightness,
                     GuiH::Flags::Normal, 0.0f, 2.0f);
    changed |= GuiH::Slider("Contrast", "", &tm.contrast, &default_tm.contrast,
                            GuiH::Flags::Normal, 0.0f, 5.0f);
    changed |=
        GuiH::Slider("Saturation", "", &tm.saturation, &default_tm.saturation,
                     GuiH::Flags::Normal, 0.0f, 5.0f);
    changed |= GuiH::Slider("Vignette", "", &tm.vignette, &default_tm.vignette,
                            GuiH::Flags::Normal, 0.0f, 2.0f);

    if (autoExposure) {
      bool localExposure = b.test(1);
      GuiH::Group<bool>("Auto Settings", true, [&] {
        changed |= GuiH::Checkbox("Local", "", &localExposure);
        changed |=
            GuiH::Slider("Burning White", "", &tm.Ywhite, &default_tm.Ywhite,
                         GuiH::Flags::Normal, 0.0f, 1.0f);
        changed |= GuiH::Slider("Brightness", "", &tm.key, &default_tm.key,
                                GuiH::Flags::Normal, 0.0f, 1.0f);
        b.set(1, localExposure);
        return changed;
      });
    }
    b.set(0, autoExposure);
    tm.autoExposure = b.to_ulong();
  }

  return false;  // no need to restart the renderer
}

bool Tracer::guiPathTracer() {
  bool changed = false;
  auto& pc = m_pipelineRaytrace.getPushconstant();
  changed |= ImGui::Checkbox("Use Face Normal", (bool*)&pc.useFaceNormal);
  changed |= ImGui::Checkbox("Ignore Emissive", (bool*)&pc.ignoreEmissive);
  return changed;
}

void Tracer::guiBusy() {
  static int nb_dots = 0;
  static float deltaTime = 0;
  bool show = true;
  size_t width = 270;
  size_t height = 60;

  deltaTime += ImGui::GetIO().DeltaTime;
  if (deltaTime > .25) {
    deltaTime = 0;
    nb_dots = ++nb_dots % 10;
  }

  auto size = m_scene.getSize();
  ImGui::SetNextWindowSize(ImVec2(float(width), float(height)));
  ImGui::SetNextWindowPos(ImVec2(float(size.width - width) * 0.5f,
                                 float(size.height - height) * 0.5f));

  ImGui::SetNextWindowBgAlpha(0.75f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 15.0);
  if (ImGui::Begin(
          "##notitle", &show,
          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
              ImGuiWindowFlags_NoSavedSettings |
              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove |
              ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMouseInputs)) {
    ImVec2 available = ImGui::GetContentRegionAvail();

    ImVec2 text_size = ImGui::CalcTextSize(m_busyReasonText.c_str(), nullptr,
                                           false, available.x);

    ImVec2 pos = ImGui::GetCursorPos();
    pos.x += (available.x - text_size.x) * 0.5f;
    pos.y += (available.y - text_size.y) * 0.5f;

    ImGui::SetCursorPos(pos);
    ImGui::TextWrapped("%s",
                       (m_busyReasonText + std::string(nb_dots, '.')).c_str());
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

bool Tracer::guiDenoiser() {
  // #OPTIX_D
  ImGui::Checkbox("Denoise", (bool*)&m_denoiseApply);
  ImGui::Checkbox("First Frame", &m_denoiseFirstFrame);
  ImGui::SliderInt("N-frames", &m_denoiseEveryNFrames, 1, 500);
  int denoisedFrame = -1;
  auto curFrame = m_pipelineRaytrace.getFrame();
  if (m_denoiseApply) {
    if (m_denoiseFirstFrame && (curFrame < m_denoiseEveryNFrames))
      denoisedFrame = 0;
    else if (curFrame > m_denoiseEveryNFrames)
      denoisedFrame =
          (curFrame / m_denoiseEveryNFrames) * m_denoiseEveryNFrames;
    ImGui::Text("Denoised Frame: %d", denoisedFrame);
  }
  return false;
}