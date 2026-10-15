#include "pipeline.hpp"

#include "../gfx/encoding.hpp"
#include "../gfx/resources.hpp"
#include "../gfx/pipeline_cache.hpp"
#include "../gfx/resource_cache.hpp"

#include "gx_fmt.hpp"
#include "shader_info.hpp"

#include <tracy/Tracy.hpp>

#include <memory>
#include <optional>

namespace aurora::gx {

namespace {
// The passes a config needs: an optional alpha prepass, then the main pass.
void pipeline_passes(const PipelineConfig& config, const gfx::RenderTargetLayout& layout, PipelineOptions& main,
                     std::optional<PipelineOptions>& prepass) {
  main = PipelineOptions{
      .colorUpdate = config.colorUpdate,
      .alphaUpdate = config.alphaUpdate,
      .depthUpdate = config.depthUpdate,
  };
  prepass.reset();
  if (config.alphaUpdate && config.dstAlpha != UINT32_MAX && config.dstAlpha != 0) {
    const auto usesSourceAlpha = [](GXBlendFactor factor) {
      return factor == GX_BL_SRCALPHA || factor == GX_BL_INVSRCALPHA;
    };
    const bool needsSourceAlpha = config.colorUpdate && config.blendMode == GX_BM_BLEND &&
                                  (usesSourceAlpha(config.blendFacSrc) || usesSourceAlpha(config.blendFacDst));
    if (!needsSourceAlpha) {
      main.dstAlphaMode = DstAlphaMode::Replace;
    } else if (webgpu::g_dualSourceBlendingSupported && layout.colorAttachmentCount == 1) {
      main.dstAlphaMode = DstAlphaMode::DualSource;
    } else {
      // Write alpha before RGB, no depth write
      auto alpha = main;
      alpha.dstAlphaMode = DstAlphaMode::Replace;
      alpha.colorUpdate = false;
      alpha.depthUpdate = false;
      prepass = alpha;
      main.alphaUpdate = false;
    }
  }
}

wgpu::RenderPipeline build_pass(const PipelineConfig& config, const gfx::RenderTargetLayout& layout,
                                const PipelineOptions& options, const char* passName, AsyncPipelineCallback onAsync) {
  const auto hash = xxh3_hash(layout.key, xxh3_hash(config, static_cast<HashType>(gfx::ShaderType::GX)));
  HashType shaderKey = 0;
  const auto shader = build_shader(config.shaderConfig, layout, options.dstAlphaMode, &shaderKey);
  const auto label = fmt::format("GX Pipeline {:x} {}", hash, passName);
  return build_pipeline(config, layout, options, {}, shader, label.c_str(), std::move(onAsync), shaderKey);
}
} // namespace

gfx::CompiledPipeline create_pipeline(const PipelineConfig& config, const gfx::RenderTargetLayout& layout) {
  ZoneScoped;
  PipelineOptions options;
  std::optional<PipelineOptions> prepass;
  pipeline_passes(config, layout, options, prepass);
  gfx::CompiledPipeline pipeline;
  if (prepass) {
    pipeline.prepass = build_pass(config, layout, *prepass, "alpha", {});
    if (!pipeline.prepass) {
      return {};
    }
  }
  pipeline.main = build_pass(config, layout, options, "main", {});
  if (!pipeline.main) {
    return {};
  }
  return pipeline;
}

void create_pipeline_async(const PipelineConfig& config, const gfx::RenderTargetLayout& layout,
                           std::function<void(gfx::CompiledPipeline)> done) {
  ZoneScoped;
  PipelineOptions options;
  std::optional<PipelineOptions> prepass;
  pipeline_passes(config, layout, options, prepass);
  struct State {
    gfx::CompiledPipeline result;
    int remaining;
    bool failed = false;
    std::function<void(gfx::CompiledPipeline)> done;
  };
  auto state = std::make_shared<State>(State{.remaining = prepass ? 2 : 1, .done = std::move(done)});
  const auto finish = [state](bool isPrepass, wgpu::RenderPipeline pipeline) {
    if (!pipeline) {
      state->failed = true;
    }
    (isPrepass ? state->result.prepass : state->result.main) = std::move(pipeline);
    if (--state->remaining == 0) {
      state->done(state->failed ? gfx::CompiledPipeline{} : std::move(state->result));
    }
  };
  if (prepass) {
    build_pass(config, layout, *prepass, "alpha", [finish](wgpu::RenderPipeline p) { finish(true, std::move(p)); });
  }
  build_pass(config, layout, options, "main", [finish](wgpu::RenderPipeline p) { finish(false, std::move(p)); });
}

void render(const DrawData& data, const wgpu::RenderPassEncoder& pass) {
  gfx::CompiledPipeline pipeline;
  if (!gfx::get_pipeline(data.pipeline, pipeline)) {
    return;
  }
  const auto& resources = gfx::detail::resources();
  auto& bound = gfx::g_gxPassBindState;
#ifdef AURORA_IMMEDIATES_AS_UNIFORM
  const std::array offsets{data.uniformRange.offset, data.immediateRange.offset, data.lightOffset};
#else
  pass.SetImmediates(0, &data.immediateData, sizeof(data.immediateData));
  const std::array offsets{data.uniformRange.offset, data.lightOffset};
#endif
  if (!bound.uniformBound || !std::equal(offsets.begin(), offsets.end(), bound.uniformOffsets.begin())) {
    pass.SetBindGroup(1, resources.uniformBindGroup, offsets.size(), offsets.data());
    bound.uniformBound = true;
    std::copy(offsets.begin(), offsets.end(), bound.uniformOffsets.begin());
  }
  if (data.bindGroups.textureBindGroup) {
    const auto bindGroup = gfx::find_bind_group(data.bindGroups.textureBindGroup);
    if (bindGroup.Get() != bound.textureBindGroup) {
      pass.SetBindGroup(2, bindGroup);
      bound.textureBindGroup = bindGroup.Get();
    }
  }
  // The whole index buffer stays bound; draws start at their range.
  if (data.indexCount != 0 && !bound.indexBufferBound) {
    pass.SetIndexBuffer(resources.indexBuffer, wgpu::IndexFormat::Uint16);
    bound.indexBufferBound = true;
  }
  if (data.dstAlpha != UINT32_MAX && data.dstAlpha != bound.dstAlpha) {
    const wgpu::Color color{0.f, 0.f, 0.f, data.dstAlpha / 255.f};
    pass.SetBlendConstant(&color);
    bound.dstAlpha = data.dstAlpha;
  }
  const auto draw = [&] {
    if (data.indexCount == 0) {
      pass.Draw(data.vtxCount, data.instanceCount, 0, data.firstInstance);
    } else {
      pass.DrawIndexed(data.indexCount, data.instanceCount, data.idxRange.offset / sizeof(uint16_t), 0,
                       data.firstInstance);
    }
  };
  if (pipeline.prepass) {
    gfx::bind_pipeline(pipeline.prepass, pass);
    draw();
  }
  gfx::bind_pipeline(pipeline.main, pass);
  draw();
}

} // namespace aurora::gx
