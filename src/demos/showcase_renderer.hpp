#pragma once
#include "graphics_settings.hpp"
#include "showcase_world.hpp"
#include <kstar_gfx/gfx.h>
#include <filesystem>
#include <array>
#include <memory>

namespace demo {
struct ShowcaseTimings {
    static constexpr size_t count=7;
    static constexpr std::array<const char*,count> names{
        "depth_ao","shadows","reflection","opaque","transparency","effects","post"};
    std::array<float,count> cpu_ms{},gpu_ms{};
    std::array<bool,count> gpu_valid{};
    std::array<uint64_t,count> gpu_frame_serial{};
    bool gpu_supported=false;
};
// The host owns frame begin/submission and the completion fence. resize is only
// called after that fence; candidate targets replace active targets atomically.
class ShowcaseRenderer {
public:
    ShowcaseRenderer(kgfx_device*,const std::filesystem::path&,const GraphicsSettings&);
    ~ShowcaseRenderer();
    ShowcaseRenderer(const ShowcaseRenderer&)=delete;
    ShowcaseRenderer& operator=(const ShowcaseRenderer&)=delete;
    void resize(const GraphicsSettings&);
    kgfx_texture* render(const ShowcaseFrame&,uint32_t environment=0,uint32_t diagnostic=0,float exposure=1);
    const ShowcaseTimings& timings() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
