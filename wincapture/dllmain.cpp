// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include "DXGICapture.h"
#include "DWMCapture.h"
#include <algorithm>
#include "types.h"

DXGICapture* dxcp = nullptr;
UINT dxgi_ref = 0;
thread_local CAPTURE_DATA dxgi_data = { 0 };
CRITICAL_SECTION dxgi_cri;
BOOL APIENTRY DllMain( HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        InitializeCriticalSection(&dxgi_cri);
        break;
    case DLL_PROCESS_DETACH:
        DeleteCriticalSection(&dxgi_cri);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

#define EXPORT __declspec(dllexport)

void __stdcall revice(BYTE* pBits, UINT Pitch, UINT Width, UINT Height, LONGLONG Tick) {
    if (pBits) {
        if (Tick != dxgi_data.tick) {
            size_t area = (UINT64)Width * Height;
            if (area > (UINT64)dxgi_data.Width * dxgi_data.Height) {
                if (BYTE* ndata = (BYTE*)realloc(dxgi_data.pBits, area * 4))
                    dxgi_data.pBits = ndata;
                else {
                    dxgi_data.Pitch = 0;
                    return;
                }
            }
            UINT pit = Width * 4;
            if (Pitch == pit)
                memcpy(dxgi_data.pBits, pBits, area * 4);
            else {
                auto p = dxgi_data.pBits;
                for (UINT i = 0; i < Height; i++) {
                    memcpy(p, pBits, pit);
                    p += pit, pBits += Pitch;
                }
            }
            dxgi_data.Height = Height;
            dxgi_data.Width = Width;
            dxgi_data.Pitch = pit;
            if (Tick)
                dxgi_data.tick = Tick;
        }
    }
    else
        dxgi_data.tick = 0;
}

void __stdcall dxgi_freeBuffer() {
    if (dxgi_data.pBits) {
        free(dxgi_data.pBits);
        memset(&dxgi_data, 0, sizeof(CAPTURE_DATA));
    }
}


#pragma region DXGI Capture
HRESULT __stdcall dxgi_start() {
    EnterCriticalSection(&dxgi_cri);
    dxgi_ref++;
    if (dxcp) {
        DXGICapture::attach = 0;
        dxcp->AttatchToThread();
        LeaveCriticalSection(&dxgi_cri);
        return S_OK;
    }
    else {
        HRESULT hr = DXGICapture::Create(&dxcp);
        if (FAILED(hr)) {
            dxgi_ref--;
            LeaveCriticalSection(&dxgi_cri);
            return hr;
        }
        DXGICapture::attach = 0;
        dxcp->AttatchToThread();
        SleepEx(100, true);
        LeaveCriticalSection(&dxgi_cri);
        return S_OK;
    }
}

HRESULT __stdcall dxgi_capture(PCaptureCallback callback, BOX* box = nullptr, UINT index = 0) {
    EnterCriticalSection(&dxgi_cri);
    HRESULT hr = dxcp->GetFrame(callback, box, index);
    LeaveCriticalSection(&dxgi_cri);
    return hr;
}

HRESULT __stdcall dxgi_captureAndSave(CAPTURE_DATA** data, BOX* box = nullptr, UINT index = 0) {
    EnterCriticalSection(&dxgi_cri);
    HRESULT hr = dxcp->GetFrame(revice, box, index);
    LeaveCriticalSection(&dxgi_cri);
    *data = nullptr;
    if (dxgi_data.tick && (hr == S_OK || hr == DXGI_ERROR_WAIT_TIMEOUT)) {
        if (dxgi_data.Pitch)
            *data = &dxgi_data;
        else
            return E_OUTOFMEMORY;
    }
    return hr;
}

void __stdcall dxgi_canCachedFrame(BOOL cached) {
    EnterCriticalSection(&dxgi_cri);
    dxcp->CacheFrame(cached);
    LeaveCriticalSection(&dxgi_cri);
}

void __stdcall dxgi_setTimeout(UINT timeout) {
    EnterCriticalSection(&dxgi_cri);
    dxcp->SetTimeout(timeout);
    LeaveCriticalSection(&dxgi_cri);
}

void __stdcall dxgi_showCursor(BOOL show) {
    EnterCriticalSection(&dxgi_cri);
    dxcp->ShowCursor(show);
    LeaveCriticalSection(&dxgi_cri);
}

HRESULT __stdcall dxgi_waitScreenChange(int timeout, BOX* box, int index) {
    EnterCriticalSection(&dxgi_cri);
    HRESULT hr = dxcp->WaitChanged(timeout, box, index);
    LeaveCriticalSection(&dxgi_cri);
    return hr;
}

HRESULT __stdcall dxgi_reset() {
    EnterCriticalSection(&dxgi_cri);
    HRESULT hr = dxcp->Reset();
    LeaveCriticalSection(&dxgi_cri);
    return hr;
}

UINT __stdcall dxgi_end() {
    EnterCriticalSection(&dxgi_cri);
    if (dxgi_ref) {
        dxcp->CacheFrame(FALSE);
        dxgi_ref--;
        if (dxgi_ref == 0) {
            delete dxcp;
            dxcp = nullptr;
        }
    }
    if (dxgi_data.pBits) {
        free(dxgi_data.pBits);
        memset(&dxgi_data, 0, sizeof(CAPTURE_DATA));
    }
    LeaveCriticalSection(&dxgi_cri);
    return dxgi_ref;
}

void __stdcall dxgi_releaseTexture(int index = -1) {
    EnterCriticalSection(&dxgi_cri);
    dxcp->releaseTexture(index);
    LeaveCriticalSection(&dxgi_cri);
}
#pragma endregion DXGI Capture


#pragma region DWM Capture
HRESULT __stdcall dwm_init(DWMCapture** dwm) {
    HRESULT hr;
    auto p = new DWMCapture;
    if (FAILED(hr = p->Init())) {
        delete p;
        return hr;
    }
    *dwm = p;
    return S_OK;
}

HRESULT __stdcall dwm_capture(DWMCapture* dwm, HWND hwnd, BOX* box, CAPTURE_DATA* data) {
    if (!dwm) return E_FAIL;
    return dwm->CaptureWindow(hwnd, box, data);
}

void __stdcall dwm_releaseTexture(DWMCapture* dwm) {
    dwm->releaseTexture();
}

void __stdcall dwm_free(DWMCapture* dwm) {
    if (dwm)
        delete dwm;
}
#pragma endregion DWM Capture

void __stdcall copyBitmapData(BITMAP_DATA* bmp, BYTE* data, LONG step, RECT* roi) {
    bmp->fillData(data, roi, step);
}

#pragma region pic color

BOOL bmp24to32(BITMAP_DATA* bmp, BITMAP_DATA* bmp32) {
    if (bmp->BytesPixel != 3 || bmp32->Pitch * bmp32->Height < 4 * bmp->Width * bmp->Height)
        return FALSE;
    union {
        UINT* s32;
        BYTE* s8;
    };
    union {
        UINT* d32;
        BYTE* d8;
    };
    auto src = bmp->pBits;
    auto line = 4 * bmp->Width;
    bool sp = (bmp->Width * 3 < bmp->Pitch);
    d8 = bmp32->pBits;
    for (int i = sp ? 0 : 1; i < bmp->Height; i++, src += bmp->Pitch, d8 += line) {
        s8 = src;
        for (int j = 0; j < bmp->Width; j++, s8 += 3)
            d32[j] = *s32 | 0xff000000;
    }
    if (!sp) {
        s8 = src;
        for (int j = 0; j < bmp->Width - 1; j++, s8 += 3, d32++)
            *d32 = *s32 | 0xff000000;
        d8[0] = s8[0], d8[1] = s8[1], d8[2] = s8[2], d8[3] = 0xff;
    }
    bmp32->updateDesc(bmp, line, 4);
    return TRUE;
}

BOOL bmp32to24(BITMAP_DATA* bmp, BITMAP_DATA* bmp24, UINT* alpha) {
    if (bmp->BytesPixel != 4)
        return FALSE;
    auto sp = bmp->Width * 3;
    auto pitch = ((sp + 3) >> 2) << 2;
    if (bmp24->Pitch * bmp24->Height < pitch * bmp->Height && bmp24->Pitch * bmp24->Height < (pitch = sp) * bmp->Height)
        return FALSE;
    sp = pitch - sp;
    union {
        BYTE* p8;
        UINT* p32;
    };
    union {
        BYTE* s8;
        UINT* s32;
    };
    auto dst = bmp24->pBits;
    auto cl = !alpha ? 0 : *alpha & 0xffffff;
    s8 = bmp->pBits;
    for (int i = sp ? 0 : 1; i < bmp->Height; i++, s8 += bmp->Pitch, dst += pitch) {
        p8 = dst;
        for (int j = 0; j < bmp->Width; j++, p8 += 3)
            *p32 = alpha && s32[j] < 0x01000000 ? cl : s32[j];
    }
    if (sp) {
        p8 = bmp24->pBits + 3 * bmp->Width;
        for (int i = 0; i < bmp->Height; i++, p8 += pitch)
            memset(p8, 0, sp);
    }
    else {
        p8 = dst;
        for (int j = 0; j < bmp->Width - 1; j++, p8 += 3, s32++)
            *p32 = alpha && *s32 < 0x01000000 ? cl : *s32;
        p8[0] = s8[0], p8[1] = s8[1], p8[2] = s8[2];
    }
    bmp24->updateDesc(bmp, pitch, 3);
    return TRUE;
}

BOOL __stdcall cvtGray(BITMAP_DATA* src, BITMAP_DATA* dst, UINT* rgb) {
    if (src->BytesPixel == 2)
        return FALSE;
    auto line = (src->Width + 3) >> 2 << 2;
    if (dst->Pitch * dst->Height < line * src->Height && dst->Pitch * dst->Height < (line = src->Width) * src->Height )
        return FALSE;
    if (src->BytesPixel == 1) {
        if (src->pBits != dst->pBits || src->Pitch != line)
            src->fillData(dst->pBits, NULL, line), dst->updateDesc(src, line, 1);
        return TRUE;
    }
    UINT r = 19595, g = 38469, b = 7472, h;
    auto mode = (UINT_PTR)rgb;
    union {
        BYTE* s8;
        PCOLOR32 scl;
    };
    auto s = src->pBits, d = dst->pBits;
    auto x = 1.0 / 2.2;
    switch (mode)
    {
    case 1: //Adobe RGB (1998) [gamma=2.20] 
        for (int i = 0; i < src->Height; i++, s += src->Pitch, d += line) {
            s8 = s;
            for (int j = 0; j < src->Width; j++, s8 += src->BytesPixel)
                d[j] = BYTE(pow(pow(scl->r, 2.2) * 0.2973 + pow(scl->g, 2.2) * 0.6274 + pow(scl->b, 2.2) * 0.0753, x));
        }
        break;
    default:
        if (mode < 65536)
            return FALSE;
        r = rgb[0], g = rgb[1], b = rgb[2], h = r + g + b;
        r = ((__int64)r << 16) / h;
        g = ((__int64)g << 16) / h;
        b = ((__int64)b << 16) / h;
    case 0:
        for (int i = 0; i < src->Height; i++, s += src->Pitch, d += line) {
            s8 = s;
            for (int j = 0; j < src->Width; j++, s8 += src->BytesPixel)
                d[j] = (scl->r * r + scl->g * g + scl->b * b) >> 16;
        }
        break;
    }
    dst->updateDesc(src, line, 1);
    return TRUE;
}

BYTE GetMeanThreshold(UINT HistGram[]) {
    UINT64 Sum = 0, Amount = 0;
    for (int Y = 0; Y < 256; Y++)
        Amount += HistGram[Y], Sum += Y * (__int64)HistGram[Y];
    return BYTE(Sum / Amount);
}
BYTE GetOSTUThreshold(UINT HistGram[], UINT area) {
    double mu = 0, scale = 1. / area;
    for (int i = 0; i < 256; i++)
        mu += i * (double)HistGram[i];

    mu *= scale;
    double mu1 = 0, q1 = 0, max_sigma = 0;
    BYTE max_val = 0;

    for (int i = 0; i < 256; i++)
    {
        double p_i, q2, mu2, sigma;

        p_i = HistGram[i] * scale;
        mu1 *= q1;
        q1 += p_i;
        q2 = 1.0 - q1;

        if (min(q1, q2) < FLT_EPSILON || max(q1, q2) > 1. - FLT_EPSILON)
            continue;

        mu1 = (mu1 + i * p_i) / q1;
        mu2 = (mu - q1 * mu1) / q2;
        sigma = q1 * q2 * (mu1 - mu2) * (mu1 - mu2);
        if (sigma > max_sigma)
        {
            max_sigma = sigma;
            max_val = i;
        }
    }

    return max_val;
}
int GetIterativeBestThreshold(UINT HistGram[]) {
    int X, Iter = 0;
    UINT64 MeanValueOne, MeanValueTwo, SumOne, SumTwo, SumIntegralOne, SumIntegralTwo;
    int MinValue, MaxValue;
    int Threshold, NewThreshold;

    for (MinValue = 0; MinValue < 256 && HistGram[MinValue] == 0; MinValue++);
    for (MaxValue = 255; MaxValue > MinValue && HistGram[MaxValue] == 0; MaxValue--);
    if (MaxValue == MinValue) return MaxValue;
    if (MinValue + 1 == MaxValue) return MinValue;

    Threshold = MinValue, NewThreshold = (MaxValue + MinValue) >> 1;
    while (Threshold != NewThreshold) {
        SumOne = 0, SumIntegralOne = 0, SumTwo = 0, SumIntegralTwo = 0, Threshold = NewThreshold;
        for (X = MinValue; X <= Threshold; X++)
            SumIntegralOne += (UINT64)HistGram[X] * X, SumOne += HistGram[X];
        MeanValueOne = SumIntegralOne / SumOne;
        for (X = Threshold + 1; X <= MaxValue; X++)
            SumIntegralTwo += (UINT64)HistGram[X] * X, SumTwo += HistGram[X];
        MeanValueTwo = SumIntegralTwo / SumTwo;
        NewThreshold = int(MeanValueOne + MeanValueTwo) >> 1;
        if (++Iter >= 1000) return -1;
    }
    return Threshold;
}
struct THRESHOLD_PARAMS {
    union {
        struct {
            BYTE min;
            BYTE max;
        };
        struct {
            short c;
            USHORT thr;
        };
        struct {
            float k;
            int r;
        };
    };
};

enum ThresholdTypes {
    THRESH_BINARY = 0,     // dst(x,y) = src(x,y) > thresh ? maxval : 0
    THRESH_BINARY_INV = 1, // dst(x,y) = src(x,y) > thresh ? 0 : maxval
    THRESH_TRUNC = 2,      // dst(x,y) = src(x,y) > thresh > threshold : src(x,y)
    THRESH_TOZERO = 3,     // dst(x,y) = src(x,y) > thresh ? src(x,y) : 0
    THRESH_TOZERO_INV = 4, // dst(x,y) = src(x,y) > thresh ? 0 : src(x,y)
    THRESH_MASK = 7,
    THRESH_OTSU = 8,       // flag, use Otsu algorithm to choose the optimal threshold value
    THRESH_ITERATIVEBEST = 16,
    THRESH_MEAN = 32,
    THRESH_ADAPTIVE_SAUVOLA = 64
};
void SauvolaThreshold(BITMAP_DATA* src, BITMAP_DATA* dst, ThresholdTypes type, double k, int r, BYTE maxval) {
    int width = src->Width, height = src->Height, pitch = src->Pitch;

    UINT64* integralImg = (UINT64*)malloc(width * height * sizeof(UINT64));
    UINT64* integralImgSqrt = (UINT64*)malloc(width * height * sizeof(UINT64));
    int sum = 0, sqrtsum = 0;
    int i, j, index, t;
    auto s = src->pBits;

    for (i = 0, index = 0; i < height; i++, s += src->Pitch, index += width) {
        sum = 0, sqrtsum = 0;
        for (j = 0; j < width; j++) {
            sum += s[j], sqrtsum += s[j] * s[j];
            t = index + j;
            if (i == 0)
                integralImg[t] = sum, integralImgSqrt[t] = sqrtsum;
            else
                integralImgSqrt[t] = integralImgSqrt[t - width] + sqrtsum, integralImg[t] = integralImg[t - width] + sum;
        }
    }
    //Calculate the mean and standard deviation using the integral image

    BYTE threshold;
    int xmin, ymin, xmax, ymax;
    double mean, std, diff, sqdiff, area;
    auto d = dst->pBits;
    for (j = 0, s = src->pBits; j < height; j++, d += dst->Pitch, s += src->Pitch) {
        for (i = 0; i < width; i++) {
            xmin = max(0, i - r);
            ymin = max(0, j - r);
            xmax = min(width - 1, i + r);
            ymax = min(height - 1, j + r);
            area = (xmax - xmin + 1) * (ymax - ymin + 1);
            if (area <= 1) {
                d[i] = (type == THRESH_TRUNC || type == THRESH_BINARY) ? maxval : (type == THRESH_TOZERO_INV || type == THRESH_BINARY_INV) ? 0 : s[i];
                continue;
            }
            diff = (double)integralImg[ymax * width + xmax], sqdiff = (double)integralImgSqrt[ymax * width + xmax];
            if (xmin * ymin)
                diff += integralImg[(ymin - 1) * width + xmin - 1], sqdiff += integralImgSqrt[(ymin - 1) * width + xmin - 1];
            if (xmin)
                diff -= integralImg[ymax * width + xmin - 1], sqdiff -= integralImgSqrt[ymax * width + xmin - 1];
            if (ymin)
                diff -= integralImg[(ymin - 1) * width + xmax], sqdiff -= integralImgSqrt[(ymin - 1) * width + xmax];
            mean = diff / area;
            std = sqrt((sqdiff - diff * diff / area) / (area - 1));
            threshold = BYTE(mean * (1 + k * ((std / 128) - 1)));
            switch (type & THRESH_MASK) {
            case THRESH_BINARY: d[i] = s[i] > threshold ? maxval : 0; break;
            case THRESH_BINARY_INV: d[i] = s[i] > threshold ? 0 : maxval; break;
            case THRESH_TOZERO: d[i] = s[i] > threshold ? s[i] : 0; break;
            case THRESH_TOZERO_INV: d[i] = s[i] > threshold ? 0 : s[i]; break;
            case THRESH_TRUNC: d[i] = s[i] > threshold ? threshold : s[i]; break;
            }
        }
    }
    free(integralImg);
    free(integralImgSqrt);
}

BOOL __stdcall threshold(BITMAP_DATA* src, BITMAP_DATA* dst, ThresholdTypes type, THRESHOLD_PARAMS* thresh, BYTE maxval = 0xff) {
    auto line = (src->Width + 3) >> 2 << 2;
    if (src->BytesPixel == 1) {
        if (dst->Pitch * dst->Height < line * src->Height && dst->Pitch * dst->Height < (line = src->Width) * src->Height)
            return FALSE;
    }
    else if (cvtGray(src, dst, NULL))
        src = dst;
    else return FALSE;

    auto d = dst->pBits, s = src->pBits;
    BYTE mi = 127, mx = 0;
    int t, flag = type & ~THRESH_MASK;
    if (flag == THRESH_ADAPTIVE_SAUVOLA) {
        double k = 0.05;
        int r = 1;
        if (thresh) {
            k = thresh->k;
            if (thresh->r > 0)
                r = thresh->r;
            else if (thresh->r < 0)
                r = max(1, src->Width >> -thresh->r);
        }
        dst->updateDesc(src, line, 1);
        SauvolaThreshold(src, dst, type, k, r, maxval);
        return TRUE;
    }
    else {
        if (flag) {
            UINT HistGram[256] = { 0 };
            s = src->pBits;
            for (int i = 0; i < src->Height; i++, s += src->Pitch)
                for (int j = 0; j < src->Width; j++)
                    HistGram[s[j]]++;
            if (type == THRESH_OTSU)
                mi = GetOSTUThreshold(HistGram, src->Width * src->Height);
            else if (type == THRESH_ITERATIVEBEST && (t = GetIterativeBestThreshold(HistGram)) > -1)
                mi = BYTE(t);
            else mi = GetMeanThreshold(HistGram);
        }
        else if (thresh)
            mi = min(thresh->min, mx = thresh->max), mx = max(mi, mx);
        if (thresh) {
            t = (int)mi + thresh->c;
            mi = min(255, max(0, t));
            thresh->thr = mi;
        }

        dst->updateDesc(src, line, 1);
        s = src->pBits, d = dst->pBits;
        flag = type & THRESH_MASK;
        if (mx) {
            for (int i = 0; i < src->Height; i++, s += src->Pitch, d += line) {
                for (int j = 0; j < src->Width; j++)
                    switch (flag) {
                    case THRESH_BINARY: d[j] = mx >= s[j] && s[j] > mi ? maxval : 0; break;
                    case THRESH_BINARY_INV: d[j] = mx >= s[j] && s[j] > mi ? 0 : maxval; break;
                    case THRESH_TOZERO: d[i] = mx >= s[j] && s[j] > mi ? s[j] : 0; break;
                    case THRESH_TOZERO_INV: d[j] = mx >= s[j] && s[j] > mi ? 0 : s[j]; break;
                    case THRESH_TRUNC: d[j] = mx >= s[j] && s[j] > mi ? mi : s[j]; break;
                    }
            }
        }
        else {
            for (int i = 0; i < src->Height; i++, s += src->Pitch, d += line) {
                for (int j = 0; j < src->Width; j++)
                    switch (flag) {
                    case THRESH_BINARY: d[j] = s[j] > mi ? maxval : 0; break;
                    case THRESH_BINARY_INV: d[j] = s[j] > mi ? 0 : maxval; break;
                    case THRESH_TOZERO: d[i] = s[j] > mi ? s[j] : 0; break;
                    case THRESH_TOZERO_INV: d[j] = s[j] > mi ? 0 : s[j]; break;
                    case THRESH_TRUNC: d[j] = s[j] > mi ? mi : s[j]; break;
                    }
            }
        }
        return TRUE;
    }
    return FALSE;
}

BOOL __stdcall cvtBytes(BITMAP_DATA* src, BITMAP_DATA* dst, short bytes) {
    if (src->BytesPixel == 2) return FALSE;
    auto line = (src->Width * bytes + 3) >> 2 << 2;
    if (dst->Pitch * dst->Height < src->Height * line && dst->Pitch * dst->Height < src->Height * (line = src->Width * bytes))
        return FALSE;
    if (bytes > 2) {
        if (src->BytesPixel == bytes) {
            if (!(src->pBits == dst->pBits && src->Pitch == line))
                src->fillData(dst->pBits, NULL, line);
            dst->updateDesc(src, line, bytes);
            return TRUE;
        }
        else if (src->BytesPixel == 1) {
            auto s = src->pBits, d = dst->pBits;
            union {
                BYTE* d8;
                UINT* d32;
            };
            auto sp = line - src->Width * bytes;
            for (int i = sp || bytes == 4 ? 0 : 1; i < src->Height; i++, s += src->Pitch, d += line) {
                d8 = d;
                for (int j = 0; j < src->Width; j++, d8 += bytes)
                    *d32 = s[j] << 16 | s[j] << 8 | s[j] | 0xff000000;
            }
            if (sp) {
                d = dst->pBits + src->Width * bytes;
                for (int i = 0; i < src->Height; i++, d += line)
                    memset(d, 0, sp);
            }
            else if (bytes == 3) {
                d8 = d;
                for (int j = 0; j < src->Width - 1; j++, d8 += bytes)
                    *d32 = s[j] << 16 | s[j] << 8 | s[j] | 0xff000000;
                d8[0] = d8[1] = d8[2] = s[src->Width - 1];
            }
            dst->updateDesc(src, line, bytes);
            return TRUE;
        }
        else if (src->BytesPixel == 4)
            return bmp32to24(src, dst, NULL);
        else
            return bmp24to32(src, dst);
    }
    else if (bytes == 1)
        return cvtGray(src, dst, NULL);
    return FALSE;
}

UINT __stdcall findAllColor(POINT poses[], UINT maxcolornum, BITMAP_DATA* bmp, UINT color, UINT variation, int direction) {
    int pitch = bmp->Pitch, bytespixel = bmp->BytesPixel;
    auto data = bmp->pBits;
    int row, col, step;
    BYTE alpha = bytespixel < 4 ? 0 : (color >> 24);

    if (!maxcolornum || !(bytespixel > 0 && bytespixel < 5))
        return 0;
    switch (direction)
    {
    case 0: //x→ y↑
        row = 0, col = 0, step = 1;
        break;
    case 1: //x← y↑
        row = 0, col = bmp->Width - 1, step = -1;
        data += col * bytespixel;
        break;
    case 2: //x→ y↓
        row = bmp->Height - 1, col = 0, step = 1;
        data += row * pitch;
        pitch = -pitch;
        break;
    case 3: //x← y↓
        row = bmp->Height - 1, col = bmp->Width - 1, step = -1;
        data += row * pitch + col * bytespixel;
        pitch = -pitch;
        break;
    default:
        return 0;
    }
    UINT num = 0;
    union {
        BYTE* p8;
        WORD* p16;
        UINT* pui;
        PCOLOR32 p32;
    };
    if (variation) {
        if (bytespixel > 2) {
            BYTE a_min, a_max, r_min, r_max, g_min, g_max, b_min, b_max;
            COLOR32 vc = { variation };
            COLOR32 cl = { color };
            if (alpha)
                a_min = max(0, (int)cl.a - vc.a), a_max = min(255, (int)cl.a + vc.a);
            else a_min = 0, a_max = 255;
            r_min = max(0, (int)cl.r - vc.r), r_max = min(255, (int)cl.r + vc.r);
            g_min = max(0, (int)cl.g - vc.g), g_max = min(255, (int)cl.g + vc.g);
            b_min = max(0, (int)cl.b - vc.b), b_max = min(255, (int)cl.b + vc.b);
            step *= bytespixel;
            for (int i = 0; i < bmp->Height; i++, data += pitch) {
                p8 = data;
                for (int j = 0; j < bmp->Width; j++, p8 += step) {
                    if ((r_min <= p32->r && p32->r <= r_max) && (g_min <= p32->g && p32->g <= g_max)
                        && (b_min <= p32->b && p32->b <= b_max) && (p32->a >= a_min && p32->a <= a_max)) {
                        poses[num++] = { abs(col - j) + bmp->Left, abs(row - i) + bmp->Top };
                        if (num == maxcolornum)
                            return num;
                    }
                }
            }
        }
        else if (bytespixel == 1) {
            color &= 0xff, variation &= 0xff;
            auto mx = min(255, color + variation), mi = max(0, (int)color - variation);
            for (int i = 0; i < bmp->Height; i++, data += pitch) {
                for (int j = 0; j < bmp->Width; j++) {
                    if (mi <= data[j] && data[j] <= mx) {
                        poses[num++] = { abs(col - j) + bmp->Left, abs(row - i) + bmp->Top };
                        if (num == maxcolornum)
                            return num;
                    }
                }
            }
        }
    }
    else if (alpha) {
        for (int i = 0; i < bmp->Height; i++, data += pitch) {
            p8 = data;
            for (int j = 0; j < bmp->Width; j++, pui += step) {
                if (*pui == color) {
                    poses[num++] = { abs(col - j) + bmp->Left, abs(row - i) + bmp->Top };
                    if (num == maxcolornum)
                        return num;
                }
            }
        }
    }
    else {
        union {
            UINT c32;
            WORD c16;
            BYTE c8;
        };
        c32 = color & 0xffffff;
        if (bytespixel > 2) {
            step *= bytespixel;
            for (int i = 0; i < bmp->Height; i++, data += pitch) {
                p8 = data;
                for (int j = 0; j < bmp->Width; j++, p8 += step) {
                    if ((*pui & 0xffffff) == c32) {
                        poses[num++] = { abs(col - j) + bmp->Left, abs(row - i) + bmp->Top };
                        if (num == maxcolornum) return num;
                    }
                }
            }
        }
        else if (bytespixel == 2) {
            for (int i = 0; i < bmp->Height; i++, data += pitch) {
                p8 = data;
                for (int j = 0; j < bmp->Width; j++, p16 += step) {
                    if (*p16 == c16) {
                        poses[num++] = { abs(col - j) + bmp->Left, abs(row - i) + bmp->Top };
                        if (num == maxcolornum) return num;
                    }
                }
            }
        }
        else {
            for (int i = 0; i < bmp->Height; i++, data += pitch) {
                p8 = data;
                for (int j = 0; j < bmp->Width; j++, p8 += step) {
                    if (*p8 == c8) {
                        poses[num++] = { abs(col - j) + bmp->Left, abs(row - i) + bmp->Top };
                        if (num == maxcolornum) return num;
                    }
                }
            }
        }
    }
    return num;
}

UINT __stdcall findAllMultiColors(POINT poses[], UINT maxcolornum, BITMAP_DATA* bmp, COLORS_DATA* colors, float similarity, UINT variation, int direction) {
    int pitch = bmp->Pitch, bytespixel = bmp->BytesPixel;
    auto data = bmp->pBits;
    int row, col, step;
    struct COLOR_RANGE {
        union {
            UINT argb;
            struct {
                BYTE b_max;
                BYTE g_max;
                BYTE r_max;
                BYTE a_max;
            };
        };
        BYTE b_min;
        BYTE g_min;
        BYTE r_min;
        BYTE a_min;
        __int64 offset;
    };
    std::vector<COLOR_RANGE> cls;
    int minx = 0x7fffffff, miny = 0x7fffffff, maxx = 0, maxy = 0;
    COLOR32 vc = { variation };
    for (int i = 0; i < colors->size; i++) {
        auto& cl = colors->arr[i];
        COLOR_RANGE cr = { 0 };
        if (variation) {
            if (cl.color.a)
                cr.a_min = max(0, (int)cl.color.a - vc.a), cr.a_max = min(255, (int)cl.color.a + vc.a);
            else cr.a_min = 0, cr.a_max = 255;
            cr.r_min = max(0, (int)cl.color.r - vc.r), cr.r_max = min(255, (int)cl.color.r + vc.r);
            cr.g_min = max(0, (int)cl.color.g - vc.g), cr.g_max = min(255, (int)cl.color.g + vc.g);
            cr.b_min = max(0, (int)cl.color.b - vc.b), cr.b_max = min(255, (int)cl.color.b + vc.b);
        }
        else
            cr.argb = cl.color.argb;
        minx = min(minx, cl.pos.x);
        miny = min(miny, cl.pos.y);
        maxx = max(maxx, cl.pos.x);
        maxy = max(maxy, cl.pos.y);
        cr.offset = cl.pos.x * (__int64)bytespixel + cl.pos.y * (__int64)pitch;
        cls.push_back(cr);
    }
    int width = maxx - minx, height = maxy - miny, w = width, h = height;
    __int64 min_off = minx * (__int64)bytespixel + miny * (__int64)pitch;
    width = bmp->Width - width;
    height = bmp->Height - height;
    if (!maxcolornum || cls.empty() || width <= 0 || height <= 0)
        return 0;
    switch (direction)
    {
    case 0: //x→ y↓
        row = 0, col = 0, step = 1;
        break;
    case 1: //x← y↓
        row = 0, col = width - 1, step = -1;
        data += col * bytespixel;
        break;
    case 2: //x→ y↑
        row = height - 1, col = 0, step = 1;
        data += row * pitch;
        pitch = -pitch;
        break;
    case 3: //x← y↑
        row = height - 1, col = width - 1, step = -1;
        data += row * pitch + col * bytespixel;
        pitch = -pitch;
        break;
    default:
        return 0;
    }
    for (auto& cl : cls) cl.offset -= min_off;
    //std::sort(cls.begin(), cls.end(), [](COLOR_RANGE& a, COLOR_RANGE& b) { return a.offset < b.offset; });
    UINT num = 0, err = 0, maxerr = (UINT)(cls.size() * (1.0f - similarity));
    union {
        BYTE* p8;
        WORD* p16;
        UINT* pui;
        PCOLOR32 p32;
    };
    auto& cl = cls[0];
    if (variation) {
        if (bytespixel > 2) {
            step *= bytespixel;
            for (auto i = 0; i < height; i++, data += pitch) {
                p8 = data;
                for (auto j = 0; j < width; j++, p8 += step) {
                    for (auto& cl0 : cls) {
                        auto& p0 = *((PCOLOR32)(p8 + cl0.offset));
                        if ((p0.r < cl0.r_min || p0.r > cl0.r_max) || (p0.g < cl0.g_min || p0.g > cl0.g_max)
                            || (p0.b < cl0.b_min || p0.b > cl0.b_max) || (p0.a < cl0.a_min || p0.a > cl0.a_max))
                            if (++err > maxerr)
                                goto next1;
                    }
                    poses[num++] = { abs(col - j) - minx + bmp->Left, abs(row - i) - miny + bmp->Top };
                    if (num == maxcolornum)
                        return num;
                next1:
                    err = 0;
                }
            }
        }
        else if (bytespixel == 1) {
            for (auto i = 0; i < height; i++, data += pitch) {
                for (auto j = 0; j < width; j++) {
                    for (auto& cl : cls) {
                        auto& p0 = data[j + cl.offset];
                        if (p0 < cl.b_min || p0 > cl.b_max)
                            if (++err > maxerr)
                                goto next0;
                    }
                    poses[num++] = { abs(col - j) - minx + bmp->Left, abs(row - i) - miny + bmp->Top };
                    if (num == maxcolornum)
                        return num;
                next0:
                    err = 0;
                }
            }
        }
    }
    else if (bytespixel > 2) {
        step *= bytespixel;
        for (auto i = 0; i < height; i++, data += pitch) {
            p8 = data;
            for (auto j = 0; j < width; j++, p8 += step) {
                for (auto& cl : cls) {
                    if ((cl.a_max && *((UINT*)(p8 + cl.offset)) != cl.argb) || (!cl.a_max && (*((UINT*)(p8 + cl.offset)) & 0xffffff) != cl.argb))
                        if (++err > maxerr)
                            goto next2;
                }
                poses[num++] = { abs(col - j) - minx + bmp->Left, abs(row - i) - miny + bmp->Top };
                if (num == maxcolornum)
                    return num;
            next2:
                err = 0;
            }
        }
    }
    else {
        UINT(*getcolor)(BYTE * p);
        step *= bytespixel;
        if (bytespixel == 2)
            getcolor = [](BYTE* p)-> UINT { return *((WORD*)p); };
        else
            getcolor = [](BYTE* p)-> UINT { return *p; };
        for (auto i = 0; i < height; i++, data += pitch) {
            p8 = data;
            for (auto j = 0; j < width; j++, p8 += step) {
                for (auto& cl : cls) {
                    if (getcolor(p8 + cl.offset) != cl.argb)
                        if (++err > maxerr)
                            goto next3;
                }
                poses[num++] = { abs(col - j) - minx + bmp->Left, abs(row - i) - miny + bmp->Top };
                if (num == maxcolornum)
                    return num;
            next3:
                err = 0;
            }
        }
    }
    return num;
}

BOOL __stdcall findColor(UINT* x, UINT* y, BITMAP_DATA* bmp, UINT color, UINT variation = 0, int direction = 0) {
    POINT poses[1];
    if (findAllColor(poses, 1, bmp, color, variation, direction)) {
        auto& p = poses[0];
        *x = p.x;
        *y = p.y;
        return TRUE;
    }
    return FALSE;
}

BOOL __stdcall findMultiColors(int* x, int* y, BITMAP_DATA* bmp, COLORS_DATA* colors, float similarity, UINT variation = 0, int direction = 0) {
    POINT poses[1];
    if (findAllMultiColors(poses, 1, bmp, colors, similarity, variation, direction)) {
        auto& p = poses[0];
        *x = p.x;
        *y = p.y;
        return TRUE;
    }
    return FALSE;
}

void removeAlpha(BITMAP_DATA* src) {
    union {
        PCOLOR32 pcl;
        BYTE* p;
    };
    p = src->pBits;
    for (int i = 0; i < src->Height; i++, p += src->Pitch)
        for (int j = 0; j < src->Width; j++)
            pcl[j].a = 0xff;
}
UINT __stdcall findAllPic(POINT poses[], UINT maxpic, BITMAP_DATA* image, BITMAP_DATA* templ, float similarity, UINT64 variation, int direction) {
    BITMAP_DATA src{ 0 }, tmp{ 0 };
    if (templ->Width > image->Width || templ->Height > image->Height || direction > 3 || direction < 0)
        return 0;
    auto left = image->Left, top = image->Top;
    if (image->BytesPixel < templ->BytesPixel) {
        if (image->BytesPixel == 2)
            return 0;
        if (image->BytesPixel == 3) {
            src.create(image->Width, image->Height, 4);
            if (cvtBytes(image, &src, 4))
                image = &src;
            else return 0;
        }
        else {
            tmp.create(templ->Width, templ->Height, templ->BytesPixel);
            if (cvtBytes(templ, &tmp, short(image->BytesPixel)))
                templ = &tmp;
            else return 0;
        }
    }
    else if (image->BytesPixel == templ->BytesPixel) {
        if (image->BytesPixel == 3) {
            tmp.create(templ->Width, templ->Height, templ->BytesPixel);
            if (cvtBytes(templ, &tmp, 4))
                templ = &tmp;
            else return 0;
            src.create(image->Width, image->Height, image->BytesPixel);
            if (cvtBytes(image, &src, 4))
                image = &src;
            else return 0;
        }
        else if (templ->BytesPixel == 4)
            removeAlpha(image);
    }
    else {
        if (templ->BytesPixel == 2)
            return 0;
        else if (templ->BytesPixel == 3) {
            tmp.create(templ->Width, templ->Height, 4);
            if (cvtBytes(templ, &tmp, 4)) {
                templ = &tmp;
                removeAlpha(image);
            }
            else
                return 0;
        }
        else {
            src.create(image->Width, image->Height, 1);
            if (cvtBytes(image, &src, 1))
                image = &src;
            else
                return 0;
        }
    }
    image->Left = left, image->Top = top;
    union {
        BYTE* s8;
        WORD* s16;
        UINT* s32;
        PCOLOR32 scl;
    };
    union {
        BYTE* d8;
        WORD* d16;
        UINT* d32;
        PCOLOR32 dcl;
    };
    BYTE* ls = image->pBits, *ld = templ->pBits, *d = ld, *s;
    int row, col, width, height, step = 1, pitch = image->Pitch, bytespixel = image->BytesPixel;
    UINT trans_color = variation >> 32;
    bool notrans_color = true;
    variation &= 0xffffffff;
    width = image->Width - templ->Width;
    height = image->Height - templ->Height;
    if (trans_color) {
        notrans_color = false;
        if (bytespixel == 4) {
            if (!tmp.pBits) {
                tmp.create(templ->Width, templ->Height, 4);
                templ->fillData(tmp.pBits, nullptr, tmp.Pitch);
                templ = &tmp;
            }
            d8 = templ->pBits;
            trans_color &= 0xffffff;
            for (int i = 0; i < templ->Height; d8 += templ->Pitch, i++)
                for (int j = 0; j < templ->Width; j++)
                    if ((d32[j] & 0xffffff) == trans_color)
                        dcl[j].a = 0;
        }
    }
    switch (direction)
    {
    case 0: //x→ y↓
        row = 0, col = 0, step = 1;
        break;
    case 1: //x← y↓
        row = 0, col = width - 1, step = -1;
        ls += col * bytespixel;
        break;
    case 2: //x→ y↑
        row = height - 1, col = 0, step = 1;
        ls += row * pitch;
        pitch = -pitch;
        break;
    case 3: //x← y↑
        row = height - 1, col = width - 1, step = -1;
        ls += row * pitch + col * bytespixel;
        pitch = -pitch;
        break;
    }
    int maxerr, err = 0, num = 0;
    int i, j, k, h, x, y;
    struct MASK {
        bool* p = nullptr;
        int width;
        int height;
        int fillw;
        int fillh;
        void create(int w, int h, int fw, int fh) {
            p = new bool[(width = w) * (height = h)];
            fillw = fw, fillh = fh;
            memset(p, 0, width * height);
        }
        inline void fill(int x, int y) {
            auto s = p + y * width + x;
            for (int i = 0; i < fillh; i++, s += width)
                memset(s, true, fillw);
        }
        inline bool notMask(int x, int y) {
            return !(p && p[x + y * width]);
        }
        ~MASK() {
            if (p) free(p);
        }
    } mask;
    d8 = d;
    if (maxpic > 1)
        mask.create(width, height, templ->Width, templ->Height);
    maxerr = int(templ->Width * templ->Height * (1 - similarity));
    step *= bytespixel;
    if (variation) {
        if (bytespixel == 4) {
            COLOR32 cl = { variation & 0xffffffff };
            for (i = 0; i < height; i++, ls += pitch) {
                s = ls;
                for (j = 0; j < width; j++, s += step) {
                    if (mask.notMask(x = abs(col - j), y = abs(row - i))) {
                        s8 = s;
                        for (h = 0; h < templ->Height; h++, s8 += image->Pitch, d8 += templ->Pitch)
                            for (k = 0; k < templ->Width; k++)
                                if (dcl[k].a == 0xff) {
                                    auto& s = scl[k], & d = dcl[k];
                                    if ((abs(s.r - d.r) > cl.r || abs(s.g - d.g) > cl.g || abs(s.b - d.b) > cl.b) && ++err > maxerr)
                                        goto next_1;
                                }
                        poses[num++] = { x + image->Left, y + image->Top };
                        if (num == maxpic)
                            return num;
                        mask.fill(x, y);
                    }
                    j += templ->Width;
                    s += step * templ->Width;
                next_1:
                    err = 0, d8 = d;
                }
            }
        }
        else if (bytespixel == 1) {
            BYTE trans_c8 = 0;
            if (trans_color)
                trans_c8 = trans_color & 0xff;
            BYTE var = variation & 0xff;
            for (i = 0; i < height; i++, ls += pitch) {
                s = ls;
                for (j = 0; j < width; j++, s += step) {
                    if (mask.notMask(x = abs(col - j), y = abs(row - i))) {
                        s8 = s;
                        for (h = 0; h < templ->Height; h++, s8 += image->Pitch, d8 += templ->Pitch)
                            for (k = 0; k < templ->Width; k++)
                                if ((notrans_color || d8[k] != trans_c8) && abs(s8[k] - d8[k]) > var && ++err > maxerr)
                                    goto next_2;
                        poses[num++] = { x + image->Left, y + image->Top };
                        if (num == maxpic)
                            return num;
                        mask.fill(x, y);
                    }
                    j += templ->Width;
                    s += step * templ->Width;
                next_2:
                    err = 0, d8 = d;
                }
            }
        }
    }
    else {
        if (bytespixel == 4) {
            for (i = 0; i < height; i++, ls += pitch) {
                s = ls;
                for (j = 0; j < width; j++, s += step) {
                    if (mask.notMask(x = abs(col - j), y = abs(row - i))) {
                        s8 = s;
                        for (h = 0; h < templ->Height; h++, s8 += image->Pitch, d8 += templ->Pitch)
                            for (k = 0; k < templ->Width; k++)
                                if (s32[k] != d32[k] && dcl[k].a == 0xff && (++err) > maxerr)
                                    goto next1;
                        poses[num++] = { x + image->Left, y + image->Top };
                        if (num == maxpic)
                            return num;
                        mask.fill(x, y);
                    }
                    j += templ->Width;
                    s += step * templ->Width;
                next1:
                    err = 0, d8 = d;
                }
            }
        }
        else if (bytespixel == 1){
            BYTE trans_c8;
            if (trans_color)
                trans_c8 = trans_color & 0xff;
            for (i = 0; i < height; i++, ls += pitch) {
                s = ls;
                for (j = 0; j < width; j++, s += step) {
                    if (mask.notMask(x = abs(col - j), y = abs(row - i))) {
                        s8 = s;
                        for (h = 0; h < templ->Height; h++, s8 += image->Pitch, d8 += templ->Pitch)
                            for (k = 0; k < templ->Width; k++)
                                if (s8[k] != d8[k] && (notrans_color || d8[k] != trans_c8) && ++err > maxerr)
                                    goto next2;
                        poses[num++] = { x + image->Left, y + image->Top };
                        if (num == maxpic)
                            return num;
                        mask.fill(x, y);
                    }
                    j += templ->Width;
                    s += step * templ->Width;
                next2:
                    err = 0, d8 = d;
                }
            }
        }
        else {
            BYTE trans_c16;
            if (trans_color)
                trans_c16 = trans_color & 0xffff;
            for (i = 0; i < height; i++, ls += pitch) {
                s = ls;
                for (j = 0; j < width; j++, s += step) {
                    if (mask.notMask(x = abs(col - j), y = abs(row - i))) {
                        s8 = s;
                        for (h = 0; h < templ->Height; h++, s8 += image->Pitch, d8 += templ->Pitch)
                            for (k = 0; k < templ->Width; k++)
                                if (s16[k] != d16[k] && (notrans_color || d16[k] != trans_c16) && ++err > maxerr)
                                    goto next3;
                        poses[num++] = { x + image->Left, y + image->Top };
                        if (num == maxpic)
                            return num;
                        mask.fill(x, y);
                    }
                    j += templ->Width;
                    s += step * templ->Width;
                next3:
                    err = 0, d8 = d;
                }
            }
        }
    }
    return num;
}

BOOL __stdcall findPic(int* x, int* y, BITMAP_DATA* image, BITMAP_DATA* templ, float similarity, UINT64 variation, int direction) {
    POINT poses[1];
    if (findAllPic(poses, 1, image, templ, similarity, variation, direction)) {
        *x = poses[0].x, * y = poses[0].y;
        return TRUE;
    }
    return FALSE;
}
#pragma endregion pic color
