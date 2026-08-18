#pragma once

#include <cstdint>

#include "Format.h"

namespace dy::RHI
{
	enum class PresentMode : uint32_t
	{
		Immediate,
		Mailbox,
		Fifo
	};

	struct SwapchainDesc
	{
		Format format = Format::Unknown;
		uint32_t minimumImageCount = 2;
		PresentMode presentMode = PresentMode::Fifo;
	};
}
