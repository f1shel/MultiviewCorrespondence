#pragma once

#include <context/context.h>
#include "alloc.h"

float* readImage(const std::string& imagePath, int& width, int& height,
                 float gamma = 1.0);
void writeImage(const std::string& imagePath, int width, int height,
                float* data);