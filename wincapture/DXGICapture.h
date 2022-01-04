#pragma once
#include <dxgi1_2.h>
#include <vector>
#include "types.h"
#include "d3d_device.h"

typedef void(__stdcall* PCaptureCallback)(BYTE* pBits, UINT Pitch, UINT Width, UINT Height, LONGLONG Tick);

class DXGICapture
{
public:
    DXGICapture();
    ~DXGICapture();

    typedef struct {
        BYTE* Buffer;
        UINT BufferSize;
        POINT Position;
        DXGI_OUTDUPL_POINTER_SHAPE_INFO ShapeInfo;
        bool Visible;
        bool Valid;
        UINT WhoUpdatedPositionLast;
        LARGE_INTEGER LastTimeStamp;
    } CURSOR_DATA;

    typedef struct {
        IDXGIOutputDuplication* dupl;
        ID3D11Texture2D* tex2d;
        DXGI_OUTPUT_DESC        output_desc;
        DXGI_OUTDUPL_DESC       outdupl_desc;
        UINT                    index;
        LONG                    x;
        LONG                    y;
        LONG                    width;
        LONG                    height;
        LONG                    left;
        LONG                    top;
        LONG                    right;
        LONG                    bottom;
        LONGLONG                tick;
        BOX                     box;
        BYTE* buf;
        UINT                    bufsize;
        bool                    inmemory;
    } DXGI_INFO;

public:
    static HRESULT Create(DXGICapture** dxgi_cap);
    HRESULT GetFrame(PCaptureCallback callback, BOX* box = nullptr, int index = 0);
    void DrawCursor(DXGI_INFO* output, DXGI_OUTDUPL_FRAME_INFO* frameInfo, BYTE* data, UINT cols);
    HRESULT Reset();
    HRESULT ResetOutput(UINT index);
    BOOL AttatchToThread();
    HRESULT WaitChanged(int timeout, BOX* box = nullptr, int index = 0);
    void SetTimeout(UINT timeout) { m_timeout = timeout; }
    void ShowCursor(BOOL show) { m_cursor = show; }
    void CacheFrame(BOOL cached) {
        if (DXGICapture::cached == cached)
            return;
        if (DXGICapture::cached = cached)
            m_cached++;
        else
            m_cached--;
    }
    void releaseTexture(int index = -1) {
        if (index < 0)
            index = DXGICapture::index;
        if (index > -1 && index < m_output.size()) {
            auto output = &m_output[index];
            RESET_OBJECT(output->tex2d);
            output->box = BOX{ 0,0,0,0 };
        }
    }
    thread_local static char attach;
    thread_local static BOOL cached;
    thread_local static LONGLONG tick;
    thread_local static int index;
    thread_local static BOX box;

private:
    HRESULT Init();
    HRESULT findOutput(DXGI_INFO*& output, BOX* box, int& index);
    HRESULT InitTexture(DXGI_INFO* output);

    ID3D11Device*           m_hDevice;
    ID3D11DeviceContext*    m_hContext;
    UINT                    m_timeout;
    BOOL                    m_cursor;
    UINT                    m_cached;

    std::vector<DXGI_INFO>  m_output;
    CURSOR_DATA m_cursorinfo = { 0 };
};
