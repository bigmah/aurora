#pragma once

#include "frame_packet.hpp"

namespace aurora::gfx {

bool bind_pipeline(PipelineRef ref, const wgpu::RenderPassEncoder& pass);
void bind_pipeline(const wgpu::RenderPipeline& pipeline, const wgpu::RenderPassEncoder& pass);

// What GX draws last bound in the current render pass, so repeated state isn't sent again
// (each WebGPU call costs a trip through JS on the web). Reset at each pass and after any other
// kind of draw.
struct GXPassBindState {
  bool uniformBound = false;
  std::array<uint32_t, 2> uniformOffsets{};
  WGPUBindGroup textureBindGroup = nullptr;
  bool indexBufferBound = false;
  uint32_t dstAlpha = UINT32_MAX;
};
extern GXPassBindState g_gxPassBindState;

namespace detail {
void encode_op(wgpu::CommandEncoder& encoder, FramePacket& frame, const FrameOp& op);
}

} // namespace aurora::gfx
