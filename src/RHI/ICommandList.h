#pragma once

#include <cstdint>

#include "Format.h"
#include "ResourceHandles.h"
#include "ResourceState.h"

namespace dy::RHI
{
	struct Viewport
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		float minDepth = 0.0f;
		float maxDepth = 1.0f;
	};

	struct Rect
	{
		int32_t x = 0;
		int32_t y = 0;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	enum class LoadOp : uint8_t
	{
		Undefined,
		Load,
		Clear,
		Discard
	};

	enum class StoreOp : uint8_t
	{
		Undefined,
		Store,
		Discard
	};

	struct ColorAttachment
	{
		TextureHandle texture = nullptr;
		uint32_t mipLevel = 0;
		uint32_t arrayLayer = 0;
		LoadOp loadOp = LoadOp::Undefined;
		StoreOp storeOp = StoreOp::Undefined;
		float clearColor[4] = {};
	};

	struct DepthStencilAttachment
	{
		TextureHandle texture = nullptr;
		uint32_t mipLevel = 0;
		uint32_t arrayLayer = 0;
		ResourceState state = ResourceState::Undefined;
		LoadOp depthLoadOp = LoadOp::Undefined;
		StoreOp depthStoreOp = StoreOp::Undefined;
		float clearDepth = 1.0f;
		LoadOp stencilLoadOp = LoadOp::Undefined;
		StoreOp stencilStoreOp = StoreOp::Undefined;
		uint32_t clearStencil = 0;
	};

	struct RenderingDesc
	{
		const ColorAttachment* colorAttachments = nullptr;
		uint32_t colorAttachmentCount = 0;
		const DepthStencilAttachment* depthStencilAttachment = nullptr;
	};

	struct ResourceBarrierDesc
	{
		BufferHandle buffer = nullptr;
		TextureHandle texture = nullptr;
		ResourceState before = ResourceState::Undefined;
		ResourceState after = ResourceState::Undefined;
		TextureSubresourceRange subresources = {};
	};

	class ICommandList
	{
	public:
		virtual void ResourceBarrier(const ResourceBarrierDesc* barriers, uint32_t count) = 0;
		virtual void BeginRendering(const RenderingDesc& desc) = 0;
		virtual void EndRendering() = 0;

		// Graphics bind와 draw는 BeginRendering/EndRendering 사이에서 기록한다.
		// Pipeline bind는 inline constant 초기화 범위를 새로 시작한다.
		virtual void BindGraphicsPipeline(PipelineHandle pipelineState) = 0;
		virtual void BindResourceSet(ResourceSetHandle resourceSet) = 0;
		virtual void BindVertexBuffer(uint32_t binding, BufferHandle buffer, uint32_t offset) = 0;
		virtual void BindIndexBuffer(BufferHandle buffer, Format format, uint32_t offset) = 0;
		virtual void SetInlineConstants(uint32_t offset, uint32_t size, const void* data) = 0;

		virtual void SetViewport(const Viewport& viewport) = 0;
		virtual void SetScissor(const Rect& rect) = 0;
		virtual void SetStencilReference(uint32_t reference) = 0;

		virtual void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance) = 0;
		virtual void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;

		virtual void Close() = 0;

	protected:
		virtual ~ICommandList() = default;
	};
}
