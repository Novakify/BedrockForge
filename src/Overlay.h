// Renderer Foundation - ImGui overlay (modified from BetterRenderDragon's ImGuiHooks.cpp; see Notice.h).
// Changes for 1.26 (GDK, Win32 window instead of UWP CoreWindow):
//   * the swapchain is found from a throw-away D3D12 device (Present/Present1/ResizeBuffers live in dxgi.dll and are
//     shared by every swapchain), so it works even when the game created its swapchain before the mod loaded
//   * the D3D12 command queue is captured from ID3D12CommandQueue::ExecuteCommandLists
//   * input comes from a Win32 window-procedure subclass (imgui_impl_win32) instead of imgui_impl_winrt
#pragma once
#include <string>

namespace rf::overlay {
bool        install();
void        uninstall();
std::string gpuName();
std::string rendererType();
} // namespace rf::overlay
