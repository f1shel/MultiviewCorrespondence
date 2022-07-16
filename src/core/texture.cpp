#include "texture.h"

#include <ImfRgba.h>
#include <ImfRgbaFile.h>
#include <shared/binding.h>

#include <nvh/nvprint.hpp>
#include <nvvk/commands_vk.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <filesystem/path.h>
using namespace filesystem;

static float* readImageEXR(const std::string& name, int* width, int* height) {
  using namespace Imf;
  using namespace Imath;
  try {
    RgbaInputFile file(name.c_str());
    Box2i dw = file.dataWindow();
    Box2i dispw = file.displayWindow();

    // OpenEXR uses inclusive pixel bounds; adjust to non-inclusive
    // (the convention pbrt uses) in the values returned.
    *width = dw.max.x - dw.min.x + 1;
    *height = dw.max.y - dw.min.y + 1;

    std::vector<Rgba> pixels(*width * *height);
    file.setFrameBuffer(&pixels[0] - dw.min.x - dw.min.y * *width, 1, *width);
    file.readPixels(dw.min.y, dw.max.y);

    auto ret = reinterpret_cast<vec4*>(malloc(sizeof(vec4) * *width * *height));
    for (int i = 0; i < *width * *height; ++i) {
      ret[i].x = pixels[i].r;
      ret[i].y = pixels[i].g;
      ret[i].z = pixels[i].b;
      ret[i].w = pixels[i].a;
    }
    return reinterpret_cast<float*>(ret);
  } catch (const std::exception& e) {
    LOGE("[x] %-20s: failed to read image file %s: %s", "Scene Error",
         name.c_str(), e.what());
    exit(1);
  }

  return NULL;
}

static void writeImageEXR(const std::string& name, const float* pixels,
                          int xRes, int yRes, int totalXRes, int totalYRes,
                          int xOffset, int yOffset) {
  using namespace Imf;
  using namespace Imath;

  Rgba* hrgba = new Rgba[xRes * yRes];
  for (int i = 0; i < xRes * yRes; ++i)
    hrgba[i] = Rgba(pixels[4 * i], pixels[4 * i + 1], pixels[4 * i + 2],
                    pixels[4 * i + 3]);

  // OpenEXR uses inclusive pixel bounds.
  Box2i displayWindow(V2i(0, 0), V2i(totalXRes - 1, totalYRes - 1));
  Box2i dataWindow(V2i(xOffset, yOffset),
                   V2i(xOffset + xRes - 1, yOffset + yRes - 1));

  try {
    RgbaOutputFile file(name.c_str(), displayWindow, dataWindow, WRITE_RGB);
    file.setFrameBuffer(hrgba - xOffset - yOffset * xRes, 1, xRes);
    file.writePixels(yRes);
  } catch (const std::exception& exc) {
    printf("Error writing \"%s\": %s", name.c_str(), exc.what());
  }

  delete[] hrgba;
}

float* readImage(const std::string& imagePath, int& width, int& height,
                 float gamma) {
  static std::set<std::string> supportExtensions = {"hdr", "exr", "jpg", "png"};
  std::string ext = path(imagePath).extension();
  if (!supportExtensions.count(ext)) {
    LOG_ERROR(
        "{}: textures only support extensions (hdr exr jpg png) "
        "while [{}] "
        "is passed in",
        "Scene", ext);
    exit(1);
  }
  void* pixels = nullptr;
  // High dynamic range image
  if (ext == "hdr")
    pixels = (void*)stbi_loadf(imagePath.c_str(), &width, &height, nullptr,
                               STBI_rgb_alpha);
  else if (ext == "exr")
    pixels = readImageEXR(imagePath, &width, &height);
  // 32bit image
  else {
    stbi_ldr_to_hdr_gamma(gamma);
    pixels = (void*)stbi_loadf(imagePath.c_str(), &width, &height, nullptr,
                               STBI_rgb_alpha);
    stbi_ldr_to_hdr_gamma(stbi__l2h_gamma);
  }
  // Handle failure
  if (!pixels) {
    LOGE("[x] %-20s: failed to load %s", "Scene Error", imagePath.c_str());
    exit(1);
  }
  return reinterpret_cast<float*>(pixels);
}

void writeImage(const std::string& imagePath, int width, int height,
                float* data) {
  static std::set<std::string> supportExtensions = {"hdr", "exr", "jpg",
                                                    "png", "tga", "bmp"};
  std::string ext = path(imagePath).extension();
  if (!supportExtensions.count(ext)) {
    LOG_ERROR(
        "{}: textures only support extensions (hdr exr jpg png) "
        "while [{}] "
        "is passed in",
        "Scene Error", ext);
    exit(1);
  }
  if (ext == "hdr")
    stbi_write_hdr(imagePath.c_str(), width, height, 4, data);
  else if (ext == "exr")
    writeImageEXR(imagePath, data, width, height, width, height, 0, 0);
  else {
    stbi_hdr_to_ldr_gamma(1.0);
    auto autoDestroyData = reinterpret_cast<float*>(
        STBI_MALLOC(width * height * 4 * sizeof(float)));
    memcpy(autoDestroyData, data, width * height * 4 * sizeof(float));
    auto ldrData = stbi__hdr_to_ldr(autoDestroyData, width, height, 4);
    if (ext == "jpg")
      stbi_write_jpg(imagePath.c_str(), width, height, 4, ldrData, 0);
    else if (ext == "png")
      stbi_write_png(imagePath.c_str(), width, height, 4, ldrData, 0);
    else if (ext == "tga")
      stbi_write_tga(imagePath.c_str(), width, height, 4, ldrData);
    else if (ext == "bmp")
      stbi_write_bmp(imagePath.c_str(), width, height, 4, ldrData);
    stbi_hdr_to_ldr_gamma(stbi__h2l_gamma_i);
  }
}
