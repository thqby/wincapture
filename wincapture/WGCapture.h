#pragma once
#include <Windows.h>
#include <dwmapi.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.System.h>
#include "d3d_device.h"
#include "types.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowsapp")

BOOL __stdcall wgc_supported();
BOOL __stdcall wgc_cursor_toggle_supported();
struct WGCapture* __stdcall wgc_init_window(HWND window, BOOL persistent);
struct WGCapture* __stdcall wgc_init_monitor(HMONITOR monitor, BOOL persistent);
struct WGCapture* __stdcall wgc_init_monitorindex(int index, BOOL persistent);
void __stdcall wgc_free(struct WGCapture* capture);
void __stdcall wgc_releaseTexture(struct WGCapture* capture);
BOOL __stdcall wgc_showCursor(struct WGCapture* capture, BOOL visible);
BOOL __stdcall wgc_isBorderRequired(struct WGCapture* capture, BOOL required);
BOOL __stdcall wgc_persistent(struct WGCapture* capture, BOOL persistent);
int __stdcall wgc_capture(struct WGCapture* capture, BOX* box, CAPTURE_DATA* data);

// source from libobs-winrt
struct WGCapture {
	BOOL active;
	uint32_t texture_width;
	uint32_t texture_height;

	bool cursor_visible{ true };
	bool persistent;

	D3D11_BOX client_box;
	THREAD_D3D* d3d_device;

	HWND window;
	HMONITOR monitor;

	winrt::com_ptr<ID3D11Texture2D> texture;
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device{ nullptr };
	winrt::com_ptr<ID3D11DeviceContext> context;
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool frame_pool{ nullptr };
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{ nullptr };
	winrt::Windows::Graphics::SizeInt32 last_size;
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem::Closed_revoker closed;
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker frame_arrived;
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame current_frame{ nullptr };

	//void (CALLBACK *callback)(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture, uint32_t width, uint32_t height) = nullptr;
	HANDLE capture_signal;
	CRITICAL_SECTION cri_sec;
	struct {
		winrt::Windows::Foundation::TimeSpan time;
		BOX box;
		char type;
	} last_capture;

	int get_frame(BOX* box);

	void on_closed(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const&, winrt::Windows::Foundation::IInspectable const&);

	void on_frame_arrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&);
};