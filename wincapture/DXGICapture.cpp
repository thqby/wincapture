#include "pch.h"
#include "DXGICapture.h"
#include <windows.h>

#pragma comment(lib, "dxgi.lib")

thread_local char DXGICapture::attach = 0;
thread_local BOOL DXGICapture::cached = FALSE;
thread_local LONGLONG DXGICapture::tick = 0;
thread_local int DXGICapture::index = -1;
thread_local BOX DXGICapture::box = { 0 };

DXGICapture::DXGICapture()
{
    m_hDevice = NULL;
    m_hContext = NULL;
    m_timeout = 0;
    m_cursor = FALSE;
    m_cached = 0;
}
DXGICapture::~DXGICapture()
{
    for (auto& output : m_output)
        if (output.dupl) {
            if (output.tex2d)
                output.tex2d->Release();
            output.dupl->Release();
        }
    if (m_cursorinfo.Buffer)
        free(m_cursorinfo.Buffer);
    RESET_OBJECT(m_hDevice);
    RESET_OBJECT(m_hContext);
}
HRESULT DXGICapture::Create(DXGICapture** dxgi_cap) {
    auto dxgicap = new DXGICapture;
    HRESULT hr = dxgicap->Init();
    *dxgi_cap = nullptr;
    if (SUCCEEDED(hr))
        *dxgi_cap = dxgicap;
    else
        delete dxgicap;
    return hr;
}
HRESULT DXGICapture::Init()
{
    HRESULT hr;

    if (FAILED(hr = CreateD3D11Device(&m_hDevice, &m_hContext)))
        return hr;
    //
    // Get DXGI device
    //
    IDXGIDevice* hDxgiDevice = NULL;
    hr = m_hDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&hDxgiDevice));
    if (FAILED(hr))
        return hr;

    //
    // Get DXGI adapter
    //
    IDXGIAdapter* hDxgiAdapter = NULL;
    hr = hDxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&hDxgiAdapter));
    RESET_OBJECT(hDxgiDevice);
    if (FAILED(hr))
        return hr;

    UINT ndupl = 0;
    IDXGIOutput* output = NULL;
    for (UINT i = 0; hDxgiAdapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; i++) {
        IDXGIOutput1* output1;
        DXGI_INFO info = { 0 };
        output->GetDesc(&info.output_desc);
        if (SUCCEEDED(hr = output->QueryInterface<IDXGIOutput1>(&output1))) {
            if (SUCCEEDED(hr = output1->DuplicateOutput(m_hDevice, &info.dupl)))
                info.dupl->GetDesc(&info.outdupl_desc), ndupl++;
            output1->Release();
        }
        auto rect = &info.output_desc.DesktopCoordinates;
        memcpy(&info.left, rect, sizeof(RECT));
        info.width = rect->right - rect->left, info.height = rect->bottom - rect->top;
        info.index = i;
        output->Release();
        m_output.push_back(info);
    }
    RESET_OBJECT(hDxgiAdapter);
    if (!ndupl)
        return hr;
    return S_OK;
}
HRESULT DXGICapture::Reset() {
    for (auto& output : m_output)
        if (output.dupl) {
            if (output.tex2d)
                output.tex2d->Release();
            output.dupl->Release();
        }
    m_output.clear();
    RESET_OBJECT(m_hDevice);
    RESET_OBJECT(m_hContext);
    return Init();
}
HRESULT DXGICapture::ResetOutput(UINT index) {
    if (index >= m_output.size())
        return DXGI_ERROR_NOT_FOUND;
    auto& info = m_output[index];
    RESET_OBJECT(info.dupl);
    RESET_OBJECT(info.tex2d);
    memset(&info, 0, sizeof(DXGI_INFO));

    IDXGIDevice* hDxgiDevice = NULL;
    HRESULT hr = m_hDevice->QueryInterface<IDXGIDevice>(&hDxgiDevice);
    if (FAILED(hr))
        return hr;

    //
    // Get DXGI adapter
    //
    IDXGIAdapter* hDxgiAdapter = NULL;
    hr = hDxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&hDxgiAdapter));
    hDxgiDevice->Release();
    if (FAILED(hr))
        return hr;
    IDXGIOutput* output = NULL;
    if (SUCCEEDED(hr = hDxgiAdapter->EnumOutputs(index, &output))) {
        IDXGIOutput1* output1;
        output->GetDesc(&info.output_desc);
        if (SUCCEEDED(hr = output->QueryInterface<IDXGIOutput1>(&output1))) {
            if (SUCCEEDED(hr = output1->DuplicateOutput(m_hDevice, &info.dupl)))
                info.dupl->GetDesc(&info.outdupl_desc);
            output1->Release();
        }
        auto rect = &info.output_desc.DesktopCoordinates;
        memcpy(&info.left, rect, sizeof(RECT));
        info.width = rect->right - rect->left, info.height = rect->bottom - rect->top;
        info.index = index;
        output->Release();
    }
    return hr;
}
BOOL DXGICapture::AttatchToThread()
{
    if (attach)
        return attach == 2;

    HDESK hCurrentDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hCurrentDesktop)
    {
        attach = 1;
        return FALSE;
    }

    // Attach desktop to this thread
    BOOL bDesktopAttached = SetThreadDesktop(hCurrentDesktop);
    DWORD err;
    if (!bDesktopAttached)
        err = GetLastError();
    CloseDesktop(hCurrentDesktop);
    hCurrentDesktop = NULL;
    attach = bDesktopAttached ? 2 : 1;
    return bDesktopAttached;
}
void DXGICapture::DrawCursor(DXGI_INFO* output, DXGI_OUTDUPL_FRAME_INFO* frameInfo, BYTE* data, UINT cols) {
    if (frameInfo->LastMouseUpdateTime.QuadPart) {
        bool UpdatePosition = true;

        if (!frameInfo->PointerPosition.Visible && (m_cursorinfo.WhoUpdatedPositionLast != output->index))
            UpdatePosition = false;

        if (frameInfo->PointerPosition.Visible && m_cursorinfo.Visible && (m_cursorinfo.WhoUpdatedPositionLast != output->index) && (m_cursorinfo.LastTimeStamp.QuadPart > frameInfo->LastMouseUpdateTime.QuadPart))
            UpdatePosition = false;

        if (UpdatePosition) {
            m_cursorinfo.Position.x = frameInfo->PointerPosition.Position.x + output->left;
            m_cursorinfo.Position.y = frameInfo->PointerPosition.Position.y + output->top;
            m_cursorinfo.WhoUpdatedPositionLast = output->index;
            m_cursorinfo.LastTimeStamp = frameInfo->LastMouseUpdateTime;
            m_cursorinfo.Visible = frameInfo->PointerPosition.Visible != 0;
        }

        if (frameInfo->PointerShapeBufferSize) {
            m_cursorinfo.Valid = false;
            if (frameInfo->PointerShapeBufferSize > m_cursorinfo.BufferSize) {
                auto p = realloc(m_cursorinfo.Buffer, frameInfo->PointerShapeBufferSize);
                if (!p)
                    return;
                m_cursorinfo.Buffer = (BYTE*)p;
                m_cursorinfo.BufferSize = frameInfo->PointerShapeBufferSize;
            }
            UINT requiredBufferSize;
            if (FAILED(output->dupl->GetFramePointerShape(m_cursorinfo.BufferSize, m_cursorinfo.Buffer, &requiredBufferSize, &m_cursorinfo.ShapeInfo)))
                return;
            m_cursorinfo.Valid = true;
        }
    }

    if (m_cursor && data && m_cursorinfo.Visible && m_cursorinfo.Valid) {
        int cursorWidth = m_cursorinfo.ShapeInfo.Width;
        int cursorHeight = m_cursorinfo.ShapeInfo.Height;
        int relativeLeft = m_cursorinfo.Position.x - output->left;
        int relativeTop = m_cursorinfo.Position.y - output->top;
        int skipx = 0, skipy = 0;
        if (m_cursorinfo.ShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME)
            cursorHeight /= 2;
        if (relativeLeft < 0)
            skipx = -relativeLeft;
        else cursorWidth = min(output->width - relativeLeft, cursorWidth);
        if (relativeTop < 0)
            skipy = -relativeTop;
        else cursorHeight = min(output->height - relativeTop, cursorHeight);
        UINT* screenBuffer = reinterpret_cast<UINT*>(data);
        UINT* cursorBuffer = reinterpret_cast<UINT*>(m_cursorinfo.Buffer);
        switch (m_cursorinfo.ShapeInfo.Type) {
        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
            for (int row = skipy; row < cursorHeight; row++) {
                BYTE mask = 0x80 >> (skipx % 8);
                for (int col = skipx; col < cursorWidth; col++) {
                    // Get masks using apropriate offsets.
                    BYTE andMask = m_cursorinfo.Buffer[row * m_cursorinfo.ShapeInfo.Pitch + col / 8] & mask;
                    BYTE xorMask = m_cursorinfo.Buffer[(row + cursorHeight) * m_cursorinfo.ShapeInfo.Pitch + col / 8] & mask;
                    UINT andMaskVal = andMask ? 0xFFFFFFFF : 0xFF000000;
                    UINT xorMaskVal = xorMask ? 0x00FFFFFF : 0x00000000;

                    UINT& screenVal = screenBuffer[(relativeTop + row) * cols + relativeLeft + col];
                    screenVal = (screenVal & andMaskVal) ^ xorMaskVal;

                    // Adjust mask
                    mask = (mask == 0x01) ? 0x80 : (mask >> 1);
                }
            }
            break;

        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
            for (int row = 0; row < cursorHeight; row++) {
                for (int col = 0; col < cursorWidth; col++) {
                    UINT cursorVal = cursorBuffer[row * (m_cursorinfo.ShapeInfo.Pitch / sizeof(UINT)) + col];
                    // Skip black or empty value
                    if (cursorVal)
                        screenBuffer[(relativeTop + row) * cols + relativeLeft + col] = cursorVal;
                }
            }
            break;

        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR: {
            UINT curcols = m_cursorinfo.ShapeInfo.Pitch / sizeof(UINT);
            for (int row = 0; row < cursorHeight; row++) {
                for (int col = 0; col < cursorWidth; col++) {
                    UINT cursorVal = cursorBuffer[row * curcols + col];
                    UINT& screenVal = screenBuffer[(relativeTop + row) * cols + relativeLeft + col];
                    UINT maskVal = cursorVal & 0xFF000000;
                    if (maskVal)
                        screenVal = (screenVal ^ cursorVal) | 0xFF000000;
                    else
                        screenVal = cursorVal | 0xFF000000;
                }
            }
            break;
        }
        default:
            break;
        }
    }
}
HRESULT DXGICapture::findOutput(DXGI_INFO*& output, BOX* box, int& index) {
    if (index < 0) {
        if (box) {
            for (auto& out : m_output) {
                if (out.left <= box->x1 && out.top <= box->y1 && box->x2 <= out.right && box->y2 <= out.bottom) {
                    output = &out;
                    box->x1 -= out.left;
                    box->x2 -= out.left;
                    box->y1 -= out.top;
                    box->y2 -= out.top;
                    break;
                }
            }
            if (!output)
                return E_INVALIDARG;
            index = output->index;
        }
        else if (m_output.size())
            output = &m_output[0];
        if (!output)
            return DXGI_ERROR_NOT_FOUND;
    }
    else {
        if ((size_t)index >= m_output.size() || !m_output[index].dupl)
            return DXGI_ERROR_NOT_FOUND;
        output = &m_output[index];
        if (box && (box->x1 < 0 || box->y1 < 0 || box->x2 > output->width || box->y2 > output->height))
            return E_INVALIDARG;
    }
    return S_OK;
}
HRESULT DXGICapture::GetFrame(PCaptureCallback callback, BOX* box, int index)
{
    HRESULT hr;
    DXGI_INFO* output = nullptr;
    if (FAILED(hr = findOutput(output, box, index)))
        return hr;

    UINT timeout = m_timeout;
    int retry_count = 0;
    LONGLONG tick = output->tick;
    IDXGIResource* hDesktopResource = NULL;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;
    ID3D11Texture2D* tex;

nextframe:
    hr = output->dupl->AcquireNextFrame(timeout, &FrameInfo, &hDesktopResource);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            if (!tick) {
                if (callback)
                    callback(nullptr, 0, 0, 0, 0);
                return DXGI_ERROR_WAIT_TIMEOUT;
            }
            if (!box) box = (BOX*)&output->x;
            if (!output->inmemory && (box->x1 < output->box.x1 || box->y1 < output->box.y1 || box->x2 > output->box.x2 || box->y2 > output->box.y2)) {
                tick = 0;
                timeout = timeout < 1000 ? 1000 : timeout;
                goto nextframe;
            }

            bool change = false;
            if (DXGICapture::tick != output->tick) {
                DXGICapture::tick = output->tick;
                DXGICapture::index = index;
                memcpy(&DXGICapture::box, box, sizeof(BOX));
                change = true;
            }
            else if (memcmp(&DXGICapture::box, box, sizeof(BOX)) || DXGICapture::index != index) {
                memcpy(&DXGICapture::box, box, sizeof(BOX));
                DXGICapture::index = index;
                change = true;
            }
            hr = output->inmemory || output->tex2d ? S_OK : DXGI_ERROR_WAIT_TIMEOUT;
            if (callback) {
                if (output->inmemory) {
                    DXGI_MAPPED_RECT mapped;
                    if (SUCCEEDED(hr = output->dupl->MapDesktopSurface(&mapped))) {
                        if (box)
                            callback(mapped.pBits + (__int64)box->y1 * mapped.Pitch + (__int64)box->x1 * 4, mapped.Pitch, box->x2 - box->x1, box->y2 - box->y1, output->tick);
                        else callback(mapped.pBits, mapped.Pitch, output->width, output->height, output->tick);
                        output->dupl->UnMapDesktopSurface();
                    }
                }
                else if (hr == S_OK) {
                    D3D11_MAPPED_SUBRESOURCE mapped;
                    if (SUCCEEDED(hr = m_hContext->Map(output->tex2d, 0, D3D11_MAP_READ_WRITE, 0, &mapped))) {
                        if (box)
                            callback((BYTE*)mapped.pData + (__int64)box->y1 * mapped.RowPitch + (__int64)box->x1 * 4, mapped.RowPitch, box->x2 - box->x1, box->y2 - box->y1, output->tick);
                        else callback((BYTE*)mapped.pData, mapped.RowPitch, output->width, output->height, output->tick);
                        m_hContext->Unmap(output->tex2d, 0);
                    }
                }
            }
            return change ? hr : DXGI_ERROR_WAIT_TIMEOUT;
        }
        else if (hr == DXGI_ERROR_ACCESS_LOST && retry_count < 3 && SUCCEEDED(ResetOutput(index))) {
            retry_count++;
            timeout = timeout < 1000 ? 1000 : timeout, tick = 0;
            goto nextframe;
        }
        else
            return hr;
    }
    if (tick = FrameInfo.LastPresentTime.QuadPart)
        output->tick = tick;
    else if (m_cursor)
        output->tick = FrameInfo.LastMouseUpdateTime.QuadPart;
    
    if (!m_cursor && !m_cached && output->outdupl_desc.DesktopImageInSystemMemory) {
        DXGI_MAPPED_RECT mapped;
        output->inmemory = true;
        if (callback && SUCCEEDED(hr = output->dupl->MapDesktopSurface(&mapped))) {
            DXGICapture::index = index;
            DXGICapture::tick = output->tick;
            if (box) {
                memcpy(&DXGICapture::box, box, sizeof(BOX));
                callback(mapped.pBits + (__int64)box->y1 * mapped.Pitch + (__int64)box->x1 * 4, mapped.Pitch, box->x2 - box->x1, box->y2 - box->y1, output->tick);
            }
            else {
                memcpy(&DXGICapture::box, (BOX*)&output->x, sizeof(BOX));
                callback(mapped.pBits, mapped.Pitch, output->width, output->height, output->tick);
            }
            output->dupl->UnMapDesktopSurface();
        }
    }
    else if (SUCCEEDED(hr = hDesktopResource->QueryInterface<ID3D11Texture2D>(&tex))) {
        if (!output->tex2d && FAILED(hr = InitTexture(output))) {
            tex->Release();
            goto exit;
        }
        output->inmemory = false;
        if (box && !m_cached) {
            D3D11_BOX dbox = { box->x1, box->y1, 0, box->x2, box->y2, 1 };
            m_hContext->CopySubresourceRegion(output->tex2d, 0, box->x1, box->y1, 0, tex, 0, &dbox);
            output->box = *box;
        }
        else {
            m_hContext->CopyResource(output->tex2d, tex);
            output->box = *(BOX*)&output->x;
        }
        
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(hr = m_hContext->Map(output->tex2d, 0, D3D11_MAP_READ_WRITE, 0, &mapped))) {
            DrawCursor(output, &FrameInfo, (BYTE*)mapped.pData, mapped.RowPitch >> 2);
            if (callback) {
                DXGICapture::index = index;
                DXGICapture::tick = output->tick;
                if (box) {
                    memcpy(&DXGICapture::box, box, sizeof(BOX));
                    callback((BYTE*)mapped.pData + (__int64)box->y1 * mapped.RowPitch + (__int64)box->x1 * 4, mapped.RowPitch, box->x2 - box->x1, box->y2 - box->y1, output->tick);
                }
                else {
                    memcpy(&DXGICapture::box, (BOX*)&output->x, sizeof(BOX));
                    callback((BYTE*)mapped.pData, mapped.RowPitch, output->width, output->height, output->tick);
                }
            }
            m_hContext->Unmap(output->tex2d, 0);
        }
        tex->Release();
    }

exit:
    hDesktopResource->Release();
    output->dupl->ReleaseFrame();
    return hr;
}

HRESULT DXGICapture::WaitChanged(int timeout, BOX* box, int index) {
    HRESULT hr;
    DXGI_INFO* output = nullptr;
    IDXGIResource* hDesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;
    if (FAILED(hr = findOutput(output, box, index)))
        return hr;
    if (SUCCEEDED(hr = output->dupl->AcquireNextFrame(0, &FrameInfo, &hDesktopResource))) {
        hDesktopResource->Release(), output->dupl->ReleaseFrame();
        output->box = BOX{ 0,0,0,0 };
    }
    else if (hr == DXGI_ERROR_ACCESS_LOST)
        ResetOutput(index);
    DWORD tick = GetTickCount(), t, t1;
    UINT MetaDataSize = 0;
    BYTE* MetaDataBuffer = nullptr;
    hDesktopResource = nullptr;
    while (true) {
        hr = output->dupl->AcquireNextFrame(timeout, &FrameInfo, &hDesktopResource);
        if ((FAILED(hr))) {
            if (MetaDataBuffer)
                delete[] MetaDataBuffer;
            return hr == DXGI_ERROR_WAIT_TIMEOUT ? DXGI_ERROR_WAIT_TIMEOUT : S_OK;
        }
        if (FrameInfo.LastMouseUpdateTime.QuadPart) {
            DrawCursor(output, &FrameInfo, nullptr, 0);
            if (m_cursor)
                output->box = BOX{ 0,0,0,0 };
        }
        if (!FrameInfo.LastPresentTime.QuadPart) {
            hDesktopResource->Release();
            hDesktopResource = nullptr;
            output->dupl->ReleaseFrame();
            t = GetTickCount();
            t1 = t - tick;
            if (t1 >= timeout) {
                delete[] MetaDataBuffer;
                return DXGI_ERROR_WAIT_TIMEOUT;
            }
            timeout -= t1;
            tick = t;
            continue;
        }
        output->box = BOX{ 0,0,0,0 };
        if (!box) {
            delete[] MetaDataBuffer;
            hDesktopResource->Release();
            output->dupl->ReleaseFrame();
            return S_OK;
        }

        if (FrameInfo.TotalMetadataBufferSize) {
            if (FrameInfo.TotalMetadataBufferSize > MetaDataSize) {
                if (MetaDataBuffer) {
                    delete[] MetaDataBuffer;
                    MetaDataBuffer = nullptr;
                }
                MetaDataBuffer = new (std::nothrow) BYTE[FrameInfo.TotalMetadataBufferSize];
                if (!MetaDataBuffer) {
                    hDesktopResource->Release();
                    output->dupl->ReleaseFrame();
                    return E_OUTOFMEMORY;
                }
                MetaDataSize = FrameInfo.TotalMetadataBufferSize;
            }

            UINT BufSize = FrameInfo.TotalMetadataBufferSize;

            hr = output->dupl->GetFrameMoveRects(BufSize, reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(MetaDataBuffer), &BufSize);
            if (FAILED(hr)) {
                delete[] MetaDataBuffer;
                hDesktopResource->Release();
                output->dupl->ReleaseFrame();
                return hr;
            }
            UINT MoveCount = BufSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);

            RECT* DirtyRects = reinterpret_cast<RECT*>(MetaDataBuffer + BufSize);
            BufSize = FrameInfo.TotalMetadataBufferSize - BufSize;

            // Get dirty rectangles
            hr = output->dupl->GetFrameDirtyRects(BufSize, DirtyRects, &BufSize);
            if (FAILED(hr)) {
                delete[] MetaDataBuffer;
                hDesktopResource->Release();
                output->dupl->ReleaseFrame();
                return hr;
            }
            UINT DirtyCount = BufSize / sizeof(RECT);

            DXGI_OUTDUPL_MOVE_RECT* move_rect = reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(MetaDataBuffer);

            for (UINT i = 0; i < DirtyCount; i++) {
                auto& r = DirtyRects[i];
                if (r.bottom < box->y1 || r.top > box->y2 || r.left > box->x2 || r.right < box->x1)
                    continue;
                delete[] MetaDataBuffer;
                hDesktopResource->Release();
                output->dupl->ReleaseFrame();
                return S_OK;
            }
            for (UINT i = 0; i < MoveCount; i++) {
                auto& r = move_rect[i].DestinationRect;
                if (r.bottom < box->y1 || r.top > box->y2 || r.left > box->x2 || r.right < box->x1)
                    continue;
                delete[] MetaDataBuffer;
                hDesktopResource->Release();
                output->dupl->ReleaseFrame();
                return S_OK;
            }
        }
        hDesktopResource->Release();
        hDesktopResource = nullptr;
        output->dupl->ReleaseFrame();
        t = GetTickCount();
        t1 = t - tick;
        if (t1 >= timeout) {
            delete[] MetaDataBuffer;
            return DXGI_ERROR_WAIT_TIMEOUT;
        }
        timeout -= t1;
        tick = t;
    }
}

HRESULT DXGICapture::InitTexture(DXGI_INFO* output) {
    D3D11_TEXTURE2D_DESC frameDescriptor{};
    frameDescriptor.Usage = D3D11_USAGE_STAGING;
    frameDescriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    frameDescriptor.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    frameDescriptor.BindFlags = 0;
    frameDescriptor.MiscFlags = 0;
    frameDescriptor.MipLevels = 1;
    frameDescriptor.ArraySize = 1;
    frameDescriptor.SampleDesc.Count = 1;
    auto& rect = output->output_desc.DesktopCoordinates;
    frameDescriptor.Width = rect.right - rect.left;
    frameDescriptor.Height = rect.bottom - rect.top;
    return m_hDevice->CreateTexture2D(&frameDescriptor, NULL, &output->tex2d);
}