#include <cassert>
#include <memory>

#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IPipelineState.h"
#include "RHI/ITexture.h"

int main()
{
	using namespace dy::RHI;
	std::unique_ptr<IDevice> device(IDevice::Create(nullptr));
	assert(device != nullptr);

	ResourceAllocationCounters counters = device->GetResourceAllocationCounters();
	assert(counters.GetTotalLive() == 0u);

	IBuffer* buffer = device->CreateBuffer({ 256u, 16u, BufferUsage::Storage });
	ITexture* texture = device->CreateTexture({
		4u, 4u, 1u, 1u, Format::R8G8B8A8_UNORM, TextureUsage::ShaderResource
	});
	IPipelineState* pipeline = device->CreateGraphicsPipeline({});
	assert(buffer != nullptr && texture != nullptr && pipeline != nullptr);

	counters = device->GetResourceAllocationCounters();
	assert(counters.buffers.live == 1u && counters.buffers.created == 1u && counters.buffers.destroyed == 0u);
	assert(counters.textures.live == 1u && counters.textures.created == 1u && counters.textures.destroyed == 0u);
	assert(counters.pipelines.live == 1u && counters.pipelines.created == 1u && counters.pipelines.destroyed == 0u);

	device->DestroyBuffer(nullptr);
	device->DestroyTexture(nullptr);
	device->DestroyPipelineState(nullptr);
	device->DestroyBuffer(buffer);
	device->DestroyTexture(texture);
	device->DestroyPipelineState(pipeline);

	// Repeated destroys must be ignored before invoking delete again.
	device->DestroyBuffer(buffer);
	device->DestroyTexture(texture);
	device->DestroyPipelineState(pipeline);

	counters = device->GetResourceAllocationCounters();
	assert(counters.GetTotalLive() == 0u);
	assert(counters.buffers.created == 1u && counters.buffers.destroyed == 1u);
	assert(counters.textures.created == 1u && counters.textures.destroyed == 1u);
	assert(counters.pipelines.created == 1u && counters.pipelines.destroyed == 1u);
	return 0;
}
