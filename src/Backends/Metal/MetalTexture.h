//
//  MetalTexture.h
//  
//
//  Created by 정준혁 on 4/8/26.
//

#pragma once
#include "RHI/ITexture.h"

namespace dy::Backends
{
    class MetalDevice;

    class MetalTexture : public RHI::ITexture
    {
    public:
        MetalTexture(const RHI::TextureDesc& desc, void* device);
        ~MetalTexture() override;

        void* GetNativeTexture() const;
        [[nodiscard]] bool IsSwapchainImage() const;

    private:
        friend class MetalDevice;

        explicit MetalTexture(const RHI::TextureDesc& desc);
        void SetBackBuffer(void* texture, const RHI::TextureDesc& desc);

        struct Impl;
        Impl* m_impl = nullptr;
    };

}
