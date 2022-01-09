#include "pch.h"
#include "WGCapture.h"

extern "C" {
	HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(::IDXGIDevice* dxgiDevice, ::IInspectable** graphicsDevice);

	HRESULT __stdcall CreateDirect3D11SurfaceFromDXGISurface(::IDXGISurface* dgxiSurface, ::IInspectable** graphicsSurface);
}

struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
	IDirect3DDxgiInterfaceAccess : ::IUnknown {
	virtual HRESULT __stdcall GetInterface(GUID const& id, void** object) = 0;
};

void blog(const wchar_t* format, ...) {
	wchar_t out[4096];
	va_list args;

	va_start(args, format);
	vswprintf(out, _countof(out), format, args);
	va_end(args);
	OutputDebugStringW(out);
}

BOOL __stdcall wgc_supported() {
	try {
		/* no contract for IGraphicsCaptureItemInterop, verify 10.0.18362.0 */
		return winrt::Windows::Foundation::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 8);
	}
	catch (const winrt::hresult_error& err) {
		blog(L"wgc_supported (0x%08X): %ls", err.code().value, err.message().c_str());
		return false;
	}
	catch (...) {
		blog(L"wgc_supported (0x%08X)", winrt::to_hresult().value);
		return false;
	}
}

BOOL __stdcall wgc_cursor_toggle_supported() {
	try {
		return winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled");
	}
	catch (const winrt::hresult_error& err) {
		blog(L"wgc_cursor_toggle_supported (0x%08X): %ls", err.code().value, err.message().c_str());
		return false;
	}
	catch (...) {
		blog(L"wgc_cursor_toggle_supported (0x%08X)", winrt::to_hresult().value);
		return false;
	}
}

template<typename T>
static winrt::com_ptr<T> GetDXGIInterfaceFromObject(winrt::Windows::Foundation::IInspectable const& object)
{
	auto access = object.as<IDirect3DDxgiInterfaceAccess>();
	winrt::com_ptr<T> result;
	winrt::check_hresult(access->GetInterface(winrt::guid_of<T>(), result.put_void()));
	return result;
}

bool get_client_box(HWND window, uint32_t width, uint32_t height, D3D11_BOX* client_box)
{
	RECT client_rect{}, window_rect{};
	POINT upper_left{};

	/* check iconic (minimized) twice, ABA is very unlikely */
	bool client_box_available = !IsIconic(window) && GetClientRect(window, &client_rect) &&
		!IsIconic(window) && (client_rect.right > 0) && (client_rect.bottom > 0) &&
		(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &window_rect, sizeof(window_rect)) == S_OK) &&
		ClientToScreen(window, &upper_left);
	if (client_box_available) {
		const uint32_t left = (upper_left.x > window_rect.left) ? (upper_left.x - window_rect.left) : 0;
		client_box->left = left;

		const uint32_t top = (upper_left.y > window_rect.top) ? (upper_left.y - window_rect.top) : 0;
		client_box->top = top;

		uint32_t texture_width = 1;
		if (width > left) {
			texture_width = min(width - left, (uint32_t)client_rect.right);
		}

		uint32_t texture_height = 1;
		if (height > top) {
			texture_height = min(height - top, (uint32_t)client_rect.bottom);
		}

		client_box->right = left + texture_width;
		client_box->bottom = top + texture_height;

		client_box->front = 0;
		client_box->back = 1;

		client_box_available = (client_box->right <= width) && (client_box->bottom <= height);
	}

	return client_box_available;
}

void WGCapture::on_closed(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const&, winrt::Windows::Foundation::IInspectable const&)
{
	active = FALSE;
}

void WGCapture::on_frame_arrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
	const winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame = sender.TryGetNextFrame();
	const winrt::Windows::Graphics::SizeInt32 frame_content_size = frame.ContentSize();
	EnterCriticalSection(&cri_sec);
	current_frame = frame;
	LeaveCriticalSection(&cri_sec);
	if (!persistent) {
		SetEvent(capture_signal);
		frame_arrived.revoke();
	}

	if (frame_content_size.Width != last_size.Width ||
		frame_content_size.Height != last_size.Height) {
		frame_pool.Recreate(device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, frame_content_size);
		last_size = frame_content_size;
	}
}

int WGCapture::get_frame(BOX* box) {
	HRESULT hr;
	EnterCriticalSection(&cri_sec);
	auto frame = current_frame;
	LeaveCriticalSection(&cri_sec);
	if (frame) {
		auto time = frame.SystemRelativeTime();
		if (time == last_capture.time && (((char)box == last_capture.type) || ((UINT_PTR)box > 65535 && !memcmp(box, &last_capture.box, sizeof(BOX)))))
			return 0;
		auto frame_surface = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
		D3D11_TEXTURE2D_DESC desc;
		uint32_t w, h;
		frame_surface->GetDesc(&desc);
		last_capture.time = time;
		if ((UINT_PTR)box != 1 || get_client_box(window, desc.Width, desc.Height, &client_box)) {
			D3D11_BOX* capture_box = nullptr;
			if (!box) {
				w = desc.Width;
				h = desc.Height;
				last_capture.type = 0;
			}
			else {
				if ((UINT_PTR)box != 1) {
					client_box.left = box->x1;
					client_box.right = box->x2;
					client_box.top = box->y1;
					client_box.bottom = box->y2;
					client_box.front = 0;
					client_box.back = 1;
					last_capture.type = 2;
					last_capture.box = *box;
				}
				else
					last_capture.type = 1;
				w = client_box.right - client_box.left;
				h = client_box.bottom - client_box.top;
				capture_box = &client_box;
			}

			if (texture && (texture_width != w || texture_height != h))
				texture = nullptr;
			texture_width = w, texture_height = h;

			if (!texture) {
				D3D11_TEXTURE2D_DESC desc = { 0 };
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
				desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				desc.BindFlags = 0;
				desc.MiscFlags = 0;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.SampleDesc.Count = 1;
				desc.Width = texture_width;
				desc.Height = texture_height;
				if (FAILED(hr = d3d_device->device->CreateTexture2D(&desc, nullptr, texture.put())))
					return hr;
			}

			if (capture_box) {
				context->CopySubresourceRegion(texture.get(), 0, 0, 0, 0, frame_surface.get(), 0, capture_box);
			}
			else {
				// if they gave an SRV, we could avoid this copy
				context->CopyResource(texture.get(), frame_surface.get());
			}
			return 0;
		}
		return -1;
	}
	return -2;
}

#if WINDOWS_FOUNDATION_UNIVERSALAPICONTRACT_VERSION >= 0xc0000
static bool wgc_border_toggle_supported() {
	try {
		return winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired");
	}
	catch (const winrt::hresult_error& err) {
		blog(L"wgc_border_toggle_supported (0x%08X): %ls", err.code().value, err.message().c_str());
		return false;
	}
	catch (...) {
		blog(L"wgc_border_toggle_supported (0x%08X)", winrt::to_hresult().value);
		return false;
	}
}
#endif

static winrt::Windows::Graphics::Capture::GraphicsCaptureItem
wgc_create_item(IGraphicsCaptureItemInterop* const interop_factory, HWND window, HMONITOR monitor)
{
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = { nullptr };
	if (window) {
		try {
			const HRESULT hr = interop_factory->CreateForWindow(window, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), reinterpret_cast<void**>(winrt::put_abi(item)));
			if (FAILED(hr))
				blog(L"CreateForWindow (0x%08X)", hr);
		}
		catch (winrt::hresult_error& err) {
			blog(L"CreateForWindow (0x%08X): %ls", err.code().value, err.message().c_str());
		}
		catch (...) {
			blog(L"CreateForWindow (0x%08X)", winrt::to_hresult().value);
		}
	}
	else {
		assert(monitor);

		try {
			const HRESULT hr = interop_factory->CreateForMonitor(monitor, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), reinterpret_cast<void**>(winrt::put_abi(item)));
			if (FAILED(hr))
				blog(L"CreateForMonitor (0x%08X)", hr);
		}
		catch (winrt::hresult_error& err) {
			blog(L"CreateForMonitor (0x%08X): %ls", err.code().value, err.message().c_str());
		}
		catch (...) {
			blog(L"CreateForMonitor (0x%08X)", winrt::to_hresult().value);
		}
	}

	return item;
}

static struct WGCapture* wgc_init_internal(HWND window, HMONITOR monitor, BOOL persistent) {
	THREAD_D3D* d3d;
	try {
		if (FAILED(GetD3D11Device(&d3d))) {
			blog(L"Failed to get D3D11 device");
			return nullptr;
		}
		ID3D11Device* const d3d_device = d3d->device;
		winrt::com_ptr<IDXGIDevice> dxgi_device;

		HRESULT hr = d3d_device->QueryInterface(dxgi_device.put());
		if (FAILED(hr)) {
			d3d->release();
			blog(L"Failed to get DXGI device");
			return nullptr;
		}

		winrt::com_ptr<IInspectable> inspectable;
		hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.get(), inspectable.put());
		if (FAILED(hr)) {
			d3d->release();
			blog(L"Failed to get WinRT device");
			return nullptr;
		}

		auto activation_factory = winrt::get_activation_factory<
			winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
		auto interop_factory = activation_factory.as<IGraphicsCaptureItemInterop>();
		winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = wgc_create_item(interop_factory.get(), window, monitor);
		if (!item) {
			d3d->release();
			return nullptr;
		}

		const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
			device = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
		const winrt::Windows::Graphics::SizeInt32 size = item.Size();
		//const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool frame_pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::Create(device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
		const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool frame_pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
		const winrt::Windows::Graphics::Capture::GraphicsCaptureSession session = frame_pool.CreateCaptureSession(item);

		struct WGCapture* capture = new WGCapture{};
		capture->d3d_device = d3d;
		capture->window = window;
		capture->monitor = monitor;
		capture->item = item;
		capture->device = device;
		d3d_device->GetImmediateContext(capture->context.put());
		capture->frame_pool = frame_pool;
		capture->session = session;
		capture->last_size = size;
		capture->capture_signal = CreateEvent(NULL, TRUE, FALSE, NULL);
		capture->closed = item.Closed(winrt::auto_revoke, { capture, &WGCapture::on_closed });
		capture->persistent = persistent;
		capture->cursor_visible = true;
		capture->cursor_visible = !wgc_showCursor(capture, FALSE);
		wgc_isBorderRequired(capture, false);
		InitializeCriticalSection(&capture->cri_sec);
		capture->frame_arrived = frame_pool.FrameArrived(winrt::auto_revoke, { capture, &WGCapture::on_frame_arrived });

		session.StartCapture();
		capture->active = TRUE;

		/*
		gs_device_loss callbacks;
		callbacks.device_loss_release = wgc_device_loss_release;
		callbacks.device_loss_rebuild = wgc_device_loss_rebuild;
		callbacks.data = capture;
		//gs_register_loss_callbacks(&callbacks);
		*/
		auto tick = GetTickCount();
		while (!capture->current_frame && GetTickCount() - tick < 500)
			SleepEx(5, TRUE);
		return capture;

	}
	catch (const winrt::hresult_error& err) {
		d3d->release();
		auto e = err.code().value;
		auto s = err.message().c_str();
		blog(L"wgc_supported (0x%08X): %ls", 4234, s);
		return nullptr;
	}
	catch (...) {
		d3d->release();
		blog(L"wgc_init (0x%08X)", winrt::to_hresult().value);
		return nullptr;
	}
}

struct WGCapture* __stdcall wgc_init_window(HWND window, BOOL persistent)
{
	return wgc_init_internal(window, NULL, persistent);
}

struct WGCapture* __stdcall wgc_init_monitor(HMONITOR monitor, BOOL persistent)
{
	return wgc_init_internal(NULL, monitor, persistent);
}

struct WGCapture* __stdcall wgc_init_monitorindex(int index, BOOL persistent)
{
	struct MONITOR_FIND_DATA {
		HMONITOR monitor;
		int monitor_index;
	} data = { NULL, index };
	EnumDisplayMonitors(NULL, NULL, [](HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM lParam) -> BOOL {
		auto data = (MONITOR_FIND_DATA*)lParam;
		if (--data->monitor_index < 0) {
			data->monitor = hMonitor;
			return FALSE;
		}
		return TRUE;
		}, (LPARAM)&data);
	return data.monitor ? wgc_init_internal(NULL, data.monitor, persistent) : nullptr;
}

void __stdcall wgc_releaseTexture(struct WGCapture* capture) {
	capture->texture = nullptr;
}

void __stdcall wgc_free(struct WGCapture* capture)
{
	if (capture) {
		capture->frame_arrived.revoke();
		capture->closed.revoke();
		capture->texture = nullptr;

		try {
			capture->frame_pool.Close();
		}
		catch (winrt::hresult_error& err) {
			blog(L"Direct3D11CaptureFramePool::Close (0x%08X): %ls", err.code().value, err.message().c_str());
		}
		catch (...) {
			blog(L"Direct3D11CaptureFramePool::Close (0x%08X)", winrt::to_hresult().value);
		}

		try {
			capture->session.Close();
		}
		catch (winrt::hresult_error& err) {
			blog(L"GraphicsCaptureSession::Close (0x%08X): %ls", err.code().value, err.message().c_str());
		}
		catch (...) {
			blog(L"GraphicsCaptureSession::Close (0x%08X)", winrt::to_hresult().value);
		}
		capture->d3d_device->release();
		CloseHandle(capture->capture_signal);
		DeleteCriticalSection(&capture->cri_sec);
		delete capture;
	}
}

BOOL __stdcall wgc_persistent(struct WGCapture* capture, BOOL persistent) {
	capture->persistent = persistent;
	if (persistent && !capture->frame_arrived) {
		capture->frame_arrived = capture->frame_pool.FrameArrived(winrt::auto_revoke, { capture, &WGCapture::on_frame_arrived });
		capture->frame_pool.TryGetNextFrame();
	}
	return TRUE;
}

BOOL __stdcall wgc_isBorderRequired(struct WGCapture* capture, BOOL required)
{
	const static BOOL border_toggle_supported = wgc_border_toggle_supported();
	BOOL success = FALSE;
	
	try {
		if (border_toggle_supported) {
			//winrt::Windows::Graphics::Capture::GraphicsCaptureAccess::RequestAccessAsync(winrt::Windows::Graphics::Capture::GraphicsCaptureAccessKind::Borderless).get();
			capture->session.IsBorderRequired(!required);
			capture->session.IsBorderRequired(required);

			success = TRUE;
		}
	}
	catch (winrt::hresult_error& err) {
		blog(L"GraphicsCaptureSession::IsBorderRequired (0x%08X): %ls", err.code().value, err.message().c_str());
	}
	catch (...) {
		blog(L"GraphicsCaptureSession::IsBorderRequired (0x%08X)", winrt::to_hresult().value);
	}

	return success;
}

BOOL __stdcall wgc_showCursor(struct WGCapture* capture, BOOL visible)
{
	const static BOOL cursor_toggle_supported = wgc_cursor_toggle_supported();
	BOOL success = FALSE;

	try {
		if (cursor_toggle_supported) {
			if (capture->cursor_visible != (bool)visible) {
				capture->session.IsCursorCaptureEnabled(visible);
				capture->cursor_visible = visible;
			}

			success = TRUE;
		}
	}
	catch (winrt::hresult_error& err) {
		blog(L"GraphicsCaptureSession::IsCursorCaptureEnabled (0x%08X): %ls", err.code().value, err.message().c_str());
	}
	catch (...) {
		blog(L"GraphicsCaptureSession::IsCursorCaptureEnabled (0x%08X)", winrt::to_hresult().value);
	}

	return success;
}

int __stdcall wgc_capture(struct WGCapture* capture, BOX* box, CAPTURE_DATA* data)
{
	if (!capture || !capture->active)
		return -3;
	if (!capture->frame_arrived) {
		ResetEvent(capture->capture_signal);
		capture->frame_arrived = capture->frame_pool.FrameArrived(winrt::auto_revoke, { capture, &WGCapture::on_frame_arrived });
		capture->frame_pool.TryGetNextFrame();
		if (WaitForSingleObject(capture->capture_signal, 1000) != WAIT_OBJECT_0)
			return -2;
	}
	int r = capture->get_frame(box);
	if (!r) {
		D3D11_MAPPED_SUBRESOURCE mapped;
		auto context = capture->context.get();
		auto tex = capture->texture.get();
		if (SUCCEEDED(r = context->Map(tex, 0, D3D11_MAP_READ_WRITE, 0, &mapped))) {
			data->pBits = (BYTE*)mapped.pData;
			data->Pitch = mapped.RowPitch;
			data->Height = capture->texture_height;
			data->Width = capture->texture_width;
			data->tick = capture->last_capture.time.count();
			context->Unmap(tex, 0);
			return 0;
		}
	}
	return r;
}
