#pragma once

#include "../gfx/pipeline_cache.hpp"
#include "gx.hpp"

namespace aurora::gx {
struct DrawData {
  gfx::PipelineRef pipeline;
  GXBindGroups bindGroups;
  gfx::Range vertRange;
  gfx::Range idxRange;
  gfx::Range uniformRange;
  uint32_t lightOffset; // uniform binding 2 (any valid offset when the shader has no lighting)
  DrawImmediateData immediateData;
#ifdef AURORA_IMMEDIATES_AS_UNIFORM
  gfx::Range immediateRange;
#endif
  uint32_t vtxCount;
  uint32_t indexCount;
  uint32_t instanceCount;
  uint32_t dstAlpha;
  uint32_t firstInstance = 0; // AURORA_IMMEDIATES_AS_UNIFORM: the vertex start (see build_shader_source)
};

constexpr uint32_t GXPipelineConfigVersion = 14; // 14: DynamicAlphaRef
struct PipelineConfig {
  uint32_t version = GXPipelineConfigVersion;
  uint32_t msaaSamples = 1; // deprecated
  ShaderConfig shaderConfig;
  GXCompare depthFunc;
  GXCullMode cullMode;
  GXBlendMode blendMode;
  GXBlendFactor blendFacSrc, blendFacDst;
  GXLogicOp blendOp;
  uint32_t dstAlpha;
  uint32_t polygonOffsetBits;
  uint32_t polygonOffsetScaleBits;
  uint32_t polygonOffsetClampBits;
  bool depthCompare, depthUpdate, alphaUpdate, colorUpdate;
};
static_assert(std::has_unique_object_representations_v<PipelineConfig>);

struct PipelineOptions {
  DstAlphaMode dstAlphaMode = DstAlphaMode::None;
  bool colorUpdate = false;
  bool alphaUpdate = false;
  bool depthUpdate = false;
};

gfx::CompiledPipeline create_pipeline(const PipelineConfig& config, const gfx::RenderTargetLayout& layout);
// Compiles in the background; done gets the pipeline (empty on failure).
void create_pipeline_async(const PipelineConfig& config, const gfx::RenderTargetLayout& layout,
                           std::function<void(gfx::CompiledPipeline)> done);
void render(const DrawData& data, const wgpu::RenderPassEncoder& pass);

void queue_surface(const u8* dlStart, uint32_t dlSize, bool bigEndian) noexcept;
} // namespace aurora::gx
