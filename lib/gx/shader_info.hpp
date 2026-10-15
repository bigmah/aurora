#pragma once

#include "gx.hpp"

namespace aurora::gx {
ShaderInfo build_shader_info(const ShaderConfig& config) noexcept;
gfx::Range build_uniform(const ShaderInfo& info) noexcept;
// The lights (uniform binding 2 of group 1), for shaders with lighting.
gfx::Range build_lights() noexcept;
u8 color_channel(GXChannelID id) noexcept;
}; // namespace aurora::gx
