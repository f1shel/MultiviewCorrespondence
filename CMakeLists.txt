cmake_minimum_required(VERSION 3.9.6)
set_property(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTY VS_STARTUP_PROJECT
    MultiviewCorrespondence)
project(MultiviewCorrespondence)
if(WIN32 AND NOT GLUT_FOUND)
  add_definitions(/wd4996) #remove printf warning
  add_definitions(/wd4244) #remove double to float conversion warning
  add_definitions(/wd4305) #remove double to float truncation warning
else()
  add_definitions(-fpermissive)
endif()

set(PROJNAME ${CMAKE_PROJECT_NAME})
set(PROJ_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SOURCE_DIR ${PROJ_ROOT_DIR}/src)
set(THIRD_PARTY_DIR ${SOURCE_DIR}/ext)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ARCH "x64" CACHE STRING "CPU Architecture")
else ()
  set(ARCH "x86" CACHE STRING "CPU Architecture")
endif()
set(OUTPUT_PATH ${PROJ_ROOT_DIR}/bin_${ARCH})

#--------------------------------------------------------------------------------------------------
# Project setting
project(${PROJNAME} LANGUAGES C CXX)
message(STATUS "-------------------------------")
message(STATUS "Processing Project ${PROJNAME}:")

#--------------------------------------------------------------------------------------------------
# C++ target and defines
set(CMAKE_CXX_STANDARD 14)
add_executable(${PROJNAME})

if(MSVC)
    add_definitions(/wd26812)  # 'enum class' over 'enum'
    add_definitions(/wd26451)  # Arithmetic overflow, casting 4 byte value to 8 byte value
endif()

#--------------------------------------------------------------------------------------------------
# look for nvpro_core 1) as a sub-folder 2) at some other locations
# this cannot be put anywhere else since we still didn't find setup.cmake yet
if(NOT BASE_DIRECTORY)
  find_path(BASE_DIRECTORY
    NAMES nvpro_core/cmake/setup.cmake
    PATHS ${THIRD_PARTY_DIR}
    REQUIRED
    DOC "Directory containing nvpro_core"
    )
endif()
## Various functions and macros REQUIRED
if(EXISTS ${BASE_DIRECTORY}/nvpro_core/cmake/setup.cmake)
  include(${BASE_DIRECTORY}/nvpro_core/cmake/setup.cmake)
  include(${BASE_DIRECTORY}/nvpro_core/cmake/utilities.cmake)
else()
  message(FATAL_ERROR "could not find base directory, please set BASE_DIRECTORY to folder containing nvpro_core")
endif()

#--------------------------------------------------------------------------------------------------
# Packages
_add_package_VulkanSDK()
_add_package_ImGUI()
if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()
_add_nvpro_core_lib()

#--------------------------------------------------------------------------------------------------
# ZLib
SET(ZLIB_BUILD_STATIC_LIBS ON CACHE BOOL " " FORCE)
SET(ZLIB_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
ADD_SUBDIRECTORY(${THIRD_PARTY_DIR}/zlib)

SET(ZLIB_INCLUDE_DIR "${THIRD_PARTY_DIR}/zlib")
SET(ZLIB_LIBRARY zlibstatic)
SET_PROPERTY(TARGET zlibstatic PROPERTY FOLDER "ext")
INCLUDE_DIRECTORIES(${ZLIB_INCLUDE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/src/ext/zlib")
INCLUDE_DIRECTORIES(${ZLIB_INCLUDE_DIRS})

#--------------------------------------------------------------------------------------------------
# Filesystem
target_include_directories(${PROJNAME} PRIVATE ${THIRD_PARTY_DIR}/filesystem)

#--------------------------------------------------------------------------------------------------
# OpenEXR
SET(ILMBASE_NAMESPACE_VERSIONING OFF CACHE BOOL " " FORCE)
SET(OPENEXR_NAMESPACE_VERSIONING OFF CACHE BOOL " " FORCE)
SET(OPENEXR_BUILD_SHARED_LIBS    OFF CACHE BOOL " " FORCE)
SET(ILMBASE_BUILD_SHARED_LIBS    OFF CACHE BOOL " " FORCE)

ADD_SUBDIRECTORY(${THIRD_PARTY_DIR}/openexr)

# For std::binary_function
set_property(TARGET IlmImf-obj PROPERTY CXX_STANDARD 14)
# Remove error exception warning
set_property(TARGET Imath IlmImf PROPERTY CXX_STANDARD 14)

#--------------------------------------------------------------------------------------------------
# SpdLog
target_include_directories(${PROJNAME} PRIVATE ${THIRD_PARTY_DIR}/spdlog/include)

INCLUDE_DIRECTORIES (
  ${THIRD_PARTY_DIR}/openexr/IlmBase/Imath
  ${THIRD_PARTY_DIR}/openexr/IlmBase/Half
  ${THIRD_PARTY_DIR}/openexr/IlmBase/Iex
  ${THIRD_PARTY_DIR}/openexr/OpenEXR/IlmImf
  ${CMAKE_BINARY_DIR}/src/ext/openexr/IlmBase/config
  ${CMAKE_BINARY_DIR}/src/ext/openexr/OpenEXR/config
)
IF(WIN32)
  SET(OPENEXR_LIBS IlmImf Imath Half ${ZLIB_LIBRARY})
ELSE()
  SET(OPENEXR_LIBS IlmImf Imath Half)
ENDIF()


#--------------------------------------------------------------------------------------------------
# Memory Allocation
#   Forcing to use our memory allocator DMA (similar to Vulkan Memory Allocator (VMA))
# target_compile_definitions(${PROJNAME} PUBLIC ALLOC_VMA)
# target_compile_definitions(${PROJNAME} PUBLIC ALLOC_DMA)
# target_compile_definitions(${PROJNAME} PUBLIC ALLOC_DEDICATED)

#--------------------------------------------------------------------------------------------------
# Default definitions: PROJECT_RELDIRECTORY, ... 
_add_project_definitions(${PROJNAME})


#--------------------------------------------------------------------------------------------------
# Source files for this project
file(GLOB ENTRY
    ${SOURCE_DIR}/main.cpp)

file(GLOB SRC_LOADER
    ${SOURCE_DIR}/loader/*.h
    ${SOURCE_DIR}/loader/*.cpp)

file(GLOB SRC_TRACER
    ${SOURCE_DIR}/tracer/*.cpp
    ${SOURCE_DIR}/tracer/*.h
    ${SOURCE_DIR}/tracer/*.hpp)

file(GLOB SRC_CONTEXT
    ${SOURCE_DIR}/context/*.h
    ${SOURCE_DIR}/context/*.cpp)

file(GLOB SRC_SCENE
    ${SOURCE_DIR}/scene/*.h
    ${SOURCE_DIR}/scene/*.cpp)

file(GLOB SRC_CORE
    ${SOURCE_DIR}/core/*.h
    ${SOURCE_DIR}/core/*.cpp)

file(GLOB SRC_PIPELINE
    ${SOURCE_DIR}/pipeline/*.h
    ${SOURCE_DIR}/pipeline/*.cpp)

# Source files for this project
file(GLOB SRC_SHARED
    ${SOURCE_DIR}/shared/*.h
)

file(GLOB SRC_SHADERS_UTILS
    ${SOURCE_DIR}/shaders/utils/*.glsl
)

file(GLOB SRC_SHADERS_RAYTRACE
    ${SOURCE_DIR}/shaders/raytrace.*.rgen
    ${SOURCE_DIR}/shaders/raytrace.*.rahit
    ${SOURCE_DIR}/shaders/raytrace.*.rmiss
    ${SOURCE_DIR}/shaders/raytrace.*.rchit
)

file(GLOB SRC_SHADERS_RAYTRACE_BXDF_UTILS
    ${SOURCE_DIR}/shaders/bxdf/*.glsl
)

file(GLOB SRC_SHADERS_RAYTRACE_BXDF
    ${SOURCE_DIR}/shaders/bxdf/raytrace.*.rchit
)

file(GLOB SRC_SHADERS_GRAPHICS
    ${SOURCE_DIR}/shaders/graphics.*.frag
    ${SOURCE_DIR}/shaders/graphics.*.vert
)

file(GLOB SRC_SHADERS_POST
    ${SOURCE_DIR}/shaders/post.*.frag
    ${SOURCE_DIR}/shaders/post.*.vert
    ${SOURCE_DIR}/shaders/post.*.comp
)

# file(GLOB SRC_3RDPARTY
#     ${THIRD_PARTY_DIR}/*
# )

file(GLOB SRC_EXTRA
    ${THIRD_PARTY_DIR}/nvpro_core/third_party/tinygltf/json.hpp
    ${THIRD_PARTY_DIR}/nvpro_core/third_party/NVTX/c/include/nvtx3/nvToolsExt.h
    ${THIRD_PARTY_DIR}/nvpro_core/third_party/tinygltf/tiny_gltf.h
    ${THIRD_PARTY_DIR}/nvpro_core/third_party/tinyobjloader/tiny_obj_loader.h
    ${THIRD_PARTY_DIR}/nvpro_core/third_party/vma/include/vk_mem_alloc.h
)
set(SRC_AUTOGEN ${SRC_EXTRA} ${COMMON_SOURCE_FILES} ${PACKAGE_SOURCE_FILES})

#--------------------------------------------------------------------------------------------------
# GLSL to SPIR-V custom build
set(SAMPLES_BASESAMPLE_DEPENDENCIES OFF CACHE BOOL "Create dependies on GLSL files")
compile_glsl(
    SOURCE_FILES
        ${SRC_SHADERS_RAYTRACE}
    HEADER_FILES
        ${SRC_SHARED}
        ${SRC_SHADERS_UTILS}
    DST
        "${OUTPUT_PATH}/shaders"
    VULKAN_TARGET
        "vulkan1.3"
    HEADER OFF
    DEPENDENCY ON
)
compile_glsl(
    SOURCE_FILES
        ${SRC_SHADERS_RAYTRACE_BXDF}
    HEADER_FILES
        ${SRC_SHARED}
        ${SRC_SHADERS_UTILS}
    DST
        "${OUTPUT_PATH}/shaders"
    VULKAN_TARGET
        "vulkan1.3"
    HEADER OFF
    DEPENDENCY ON
)
compile_glsl(
    SOURCE_FILES
        ${SRC_SHADERS_GRAPHICS}
    HEADER_FILES
        ${SRC_SHARED}
        ${SRC_SHADERS_UTILS}
    DST
        "${OUTPUT_PATH}/shaders"
    VULKAN_TARGET
        "vulkan1.3"
    HEADER OFF
    DEPENDENCY ON
)
compile_glsl(
    SOURCE_FILES
        ${SRC_SHADERS_POST}
    HEADER_FILES
        ${SRC_SHARED}
        ${SRC_SHADERS_UTILS}
    DST
        "${OUTPUT_PATH}/shaders"
    VULKAN_TARGET
        "vulkan1.2"
    HEADER OFF
    DEPENDENCY ON
)


#--------------------------------------------------------------------------------------------------
# Sources
target_sources(${PROJNAME} PUBLIC
    ${ENTRY}
    ${SRC_LOADER}
    ${SRC_TRACER}
    ${SRC_CONTEXT}
    ${SRC_SCENE}
    ${SRC_CORE}
    ${SRC_PIPELINE}
    ${SRC_SHARED}
    ${SRC_SHADERS_UTILS}
    ${SRC_SHADERS_GRAPHICS}
    ${SRC_SHADERS_POST}
    ${SRC_SHADERS_RAYTRACE}
    ${SRC_AUTOGEN}
)
# include directory
target_include_directories(${PROJNAME} PUBLIC
    ${SOURCE_DIR}
)

#--------------------------------------------------------------------------------------------------
# Sub-folders in Visual Studio
source_group("autogen" FILES ${SRC_AUTOGEN})
source_group("" FILES ${ENTRY})
# source_group("third_party\\json" FILES ${SRC_3RDPARTY_JSON})
# source_group("third_party\\tqdm" FILES ${SRC_3RDPARTY_TQDM})
source_group("loader" FILES ${SRC_LOADER})
source_group("core" FILES ${SRC_CORE})
source_group("tracer" FILES ${SRC_TRACER})
source_group("context" FILES ${SRC_CONTEXT})
source_group("scene" FILES ${SRC_SCENE})
source_group("pipeline" FILES ${SRC_PIPELINE})
source_group("shared" FILES ${SRC_SHARED})
source_group("shaders" FILES ${SRC_SHADERS_RAYTRACE} ${SRC_SHADERS_GRAPHICS})
source_group("shaders\\utils" FILES ${SRC_SHADERS_UTILS})

SET_PROPERTY(TARGET
  IexMath
  eLut
  toFloat
  b44ExpLogTable
  dwaLookups
  IlmThread
  Half
  Iex
  Imath
  IlmImf
  CopyIlmBaseLibs
  IlmImf-obj
  nvpro_core
  glfw
  imgui
PROPERTY FOLDER "ext")

#--------------------------------------------------------------------------------------------------
# Linkage
target_link_libraries(${PROJNAME} ${PLATFORM_LIBRARIES} nvpro_core ${OPENEXR_LIBS} ${ZLIB_LIBRARY})

foreach(DEBUGLIB ${LIBRARIES_DEBUG})
    target_link_libraries(${PROJNAME} debug ${DEBUGLIB})
endforeach(DEBUGLIB)
foreach(RELEASELIB ${LIBRARIES_OPTIMIZED})
    target_link_libraries(${PROJNAME} optimized ${RELEASELIB})
endforeach(RELEASELIB)

# copies binaries that need to be put next to the exe files (ZLib, etc.)
_finalize_target(${PROJNAME})

# set(SCENE_SOURCE "${PROJ_ROOT_DIR}/scenes")
# set(SCENE_DESTINATION "${OUTPUT_PATH}/scenes")
# add_custom_command(
#     TARGET ${PROJECT_NAME} POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_directory ${SCENE_SOURCE} ${SCENE_DESTINATION} 
#     DEPENDS ${SCENE_SOURCE}
#     COMMENT "symbolic link resources folder from ${SCENE_SOURCE} => ${SCENE_DESTINATION}")
# file(COPY ${PROJ_ROOT_DIR}/scenes DESTINATION ${OUTPUT_PATH})