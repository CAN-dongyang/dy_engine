#pragma once

#include <cstdint>

#include "Platform/Window.h"

class ExampleWindow
{
public:
	ExampleWindow(uint32_t width, uint32_t height, const char* title)
		: m_window(width, height, title)
		, m_width(width)
		, m_height(height)
	{
	}

	[[nodiscard]] bool IsRunning() const { return m_window.IsRunning(); }
	void PollEvents() const { m_window.PollEvents(); }

	[[nodiscard]] const void* GetHandle() const { return m_window.GetHandle(); }
	[[nodiscard]] uint32_t GetWidth() const { return m_width; }
	[[nodiscard]] uint32_t GetHeight() const { return m_height; }

private:
	dy::Platform::Window m_window;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
};
