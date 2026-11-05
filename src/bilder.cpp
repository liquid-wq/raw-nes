#include "bilder.h"

#include <algorithm>
#include <windows.h>
#include <objidl.h>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <gl/gl.h>

#include <cstring>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace bilder {
namespace {

ULONG_PTR g_gdiplus = 0;

int Zweierpotenz(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

}  // namespace

bool Start() {
    if (g_gdiplus) return true;
    Gdiplus::GdiplusStartupInput eingabe;
    return Gdiplus::GdiplusStartup(&g_gdiplus, &eingabe, nullptr) == Gdiplus::Ok;
}

void Ende() {
    if (g_gdiplus) {
        Gdiplus::GdiplusShutdown(g_gdiplus);
        g_gdiplus = 0;
    }
}

bool PngDekodieren(const void* daten, size_t n, int* w, int* h, std::vector<uint8_t>* rgba) {
    if (!daten || n == 0 || !g_gdiplus) return false;
    HGLOBAL hg = ::GlobalAlloc(GMEM_MOVEABLE, n);
    if (!hg) return false;
    std::memcpy(::GlobalLock(hg), daten, n);
    ::GlobalUnlock(hg);
    IStream* strom = nullptr;
    if (FAILED(::CreateStreamOnHGlobal(hg, TRUE, &strom))) {
        ::GlobalFree(hg);
        return false;
    }

    bool ok = false;
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(strom);
    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
        const int bw = (int)bmp->GetWidth(), bh = (int)bmp->GetHeight();
        Gdiplus::Rect r(0, 0, bw, bh);
        Gdiplus::BitmapData bd;
        if (bw > 0 && bh > 0 &&
            bmp->LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
            rgba->resize((size_t)bw * bh * 4);
            for (int y = 0; y < bh; ++y) {
                const uint8_t* src = (const uint8_t*)bd.Scan0 + (ptrdiff_t)y * bd.Stride;
                uint8_t* dst = rgba->data() + (size_t)y * bw * 4;
                for (int x = 0; x < bw; ++x) {
                    // GDI+ liefert BGRA, OpenGL will RGBA.
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = src[x * 4 + 3];
                }
            }
            bmp->UnlockBits(&bd);
            *w = bw;
            *h = bh;
            ok = true;
        }
    }
    delete bmp;
    strom->Release();
    return ok;
}

Textur Hochladen(const uint8_t* rgba, int w, int h, bool pixelig) {
    Textur t;
    if (!rgba || w <= 0 || h <= 0) return t;
    const int tw = Zweierpotenz(w), th = Zweierpotenz(h);
    std::vector<uint8_t> flaeche((size_t)tw * th * 4, 0);
    for (int y = 0; y < h; ++y)
        std::memcpy(flaeche.data() + (size_t)y * tw * 4, rgba + (size_t)y * w * 4, (size_t)w * 4);

    GLuint id = 0;
    ::glGenTextures(1, &id);
    ::glBindTexture(GL_TEXTURE_2D, id);
    const GLint filter = pixelig ? GL_NEAREST : GL_LINEAR;
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, flaeche.data());

    t.id = id;
    t.w = w;
    t.h = h;
    t.u = (float)w / (float)tw;
    t.v = (float)h / (float)th;
    return t;
}

Textur PngHochladen(const void* daten, size_t n, bool pixelig) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!PngDekodieren(daten, n, &w, &h, &rgba)) return Textur();
    return Hochladen(rgba.data(), w, h, pixelig);
}

void Freigeben(Textur& t) {
    if (t.id) {
        GLuint id = t.id;
        ::glDeleteTextures(1, &id);
    }
    t = Textur();
}

bool Ressource(int id, const void** daten, size_t* n) {
    HMODULE mod = ::GetModuleHandleW(nullptr);
    HRSRC r = ::FindResourceW(mod, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!r) return false;
    HGLOBAL g = ::LoadResource(mod, r);
    if (!g) return false;
    *daten = ::LockResource(g);
    *n = ::SizeofResource(mod, r);
    return *daten != nullptr && *n > 0;
}

}  // namespace bilder
