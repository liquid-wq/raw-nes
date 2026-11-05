// RAW-NES -- RetroAchievements auf einer echten NES ueber den EverDrive N8 PRO.
//
// Einzelne Exe: Dear ImGui auf Win32 + OpenGL2, MinGW-Laufzeit statisch,
// FPGA-Kerne und Intro-Bilder als Ressourcen eingebettet. Keine Begleit-DLLs.
//
// Ablauf wie in main.cpp der Qt-Fassung: erst das Intro, rahmenlos und genau
// so gross und an derselben Stelle wie danach das Hauptfenster; dann das
// Hauptfenster.

#include <windows.h>
#include <objbase.h>
#include <gl/gl.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <thread>

#include "imgui.h"
#include "imgui_impl_opengl2.h"
#include "imgui_impl_win32.h"

#include "app.h"
#include "bilder.h"
#include "einstellungen.h"
#include "i18n.h"
#include "intro.h"
#include "plattform.h"
#include "popup.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

HDC g_dc = nullptr;
HGLRC g_rc = nullptr;
App* g_app = nullptr;

// ---------------------------------------------------------------------------
// Absturzspur. Ein Fensterprogramm, das abstuerzt, verschwindet sonst einfach.
// Geschrieben wird nach %TEMP%\rawnes_absturz.txt: Ursache, Adresse relativ
// zur Exe (damit sich die Stelle im Build wiederfinden laesst), Build-Nummer
// und die letzten Zeilen des Logs.
// ---------------------------------------------------------------------------
std::wstring AbsturzDatei() {
    wchar_t pfad[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, pfad);
    return std::wstring(pfad) + L"rawnes_absturz.txt";
}

void AbsturzMelden(const char* ursache, unsigned long code, const void* adresse) {
    static volatile LONG schonGemeldet = 0;
    if (::InterlockedExchange(&schonGemeldet, 1)) return;

    const std::wstring datei = AbsturzDatei();
    if (std::FILE* f = _wfopen(datei.c_str(), L"w")) {
        SYSTEMTIME t;
        ::GetLocalTime(&t);
        std::fprintf(f, "RAW-NES build %d -- Absturz %04d-%02d-%02d %02d:%02d:%02d\n", RAWNES_BUILD,
                     t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        std::fprintf(f, "Ursache: %s\n", ursache);
        if (code) std::fprintf(f, "Code: 0x%08lX\n", code);
        if (adresse) {
            const uintptr_t basis = (uintptr_t)::GetModuleHandleW(nullptr);
            std::fprintf(f, "Adresse: exe+0x%llX\n", (unsigned long long)((uintptr_t)adresse - basis));
        }
        if (g_app) {
            std::fprintf(f, "\n--- letzte Log-Zeilen ---\n");
            const auto& log = g_app->LogZeilen();
            const size_t ab = log.size() > 200 ? log.size() - 200 : 0;
            for (size_t i = ab; i < log.size(); ++i) std::fprintf(f, "%s\n", log[i].c_str());
        }
        std::fclose(f);
    }
    const std::string text = Fmt(T("Das Programm ist abgestürzt.\n\nEinzelheiten stehen in %1"),
                                 {plattform::Utf8(datei)});
    ::MessageBoxW(nullptr, plattform::Breit(text).c_str(), L"RAW-NES", MB_ICONERROR | MB_TOPMOST);
}

LONG WINAPI AufAbsturz(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e ? e->ExceptionRecord : nullptr;
    AbsturzMelden("Ausnahme des Systems", r ? r->ExceptionCode : 0, r ? r->ExceptionAddress : nullptr);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Unbehandelte C++-Ausnahme, z. B. in einem der Hilfsthreads.
void AufTerminate() {
    std::string ursache = "unbehandelte C++-Ausnahme";
    try {
        if (auto ep = std::current_exception()) std::rethrow_exception(ep);
    } catch (const std::exception& ex) {
        ursache += std::string(": ") + ex.what();
    } catch (...) {
    }
    AbsturzMelden(ursache.c_str(), 0, nullptr);
    ::ExitProcess(3);
}

bool CreateGlContext(HWND hwnd) {
    g_dc = ::GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int pf = ::ChoosePixelFormat(g_dc, &pfd);
    if (pf == 0) return false;
    if (!::SetPixelFormat(g_dc, pf, &pfd)) return false;
    g_rc = ::wglCreateContext(g_dc);
    if (!g_rc) return false;
    return ::wglMakeCurrent(g_dc, g_rc) != FALSE;
}

void DestroyGlContext(HWND hwnd) {
    ::wglMakeCurrent(nullptr, nullptr);
    if (g_rc) { ::wglDeleteContext(g_rc); g_rc = nullptr; }
    if (g_dc) { ::ReleaseDC(hwnd, g_dc); g_dc = nullptr; }
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    switch (msg) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) ::glViewport(0, 0, LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// Systemschriften aus dem Windows-Ordner; muessen nicht mitgeliefert werden.
std::string Schriftpfad(const char* datei) {
    wchar_t win[MAX_PATH] = {};
    ::GetWindowsDirectoryW(win, MAX_PATH);
    return plattform::Utf8(win) + "\\Fonts\\" + datei;
}

bool Vorhanden(const std::string& pfad) {
    return ::GetFileAttributesW(plattform::Breit(pfad).c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Dear ImGui versteht eine Schriftgroesse als ganze Zeilenhoehe (Ober- plus
// Unterlaenge), Qt und HTML dagegen als Em-Groesse. Bei Segoe UI ist die
// Zeilenhoehe rund 1,33 em -- ohne Ausgleich waere jede Schrift um diesen
// Faktor kleiner als in der Qt-Fassung. Der Faktor wird aus der Schriftdatei
// selbst gelesen: (ascender - descender) aus 'hhea' durch unitsPerEm aus
// 'head', genau die Groessen, die stb_truetype fuer die Zeilenhoehe benutzt.
float EmFaktor(const std::string& pfad) {
    std::FILE* f = _wfopen(plattform::Breit(pfad).c_str(), L"rb");
    if (!f) return 1.0f;
    auto u16 = [](const unsigned char* p) { return (unsigned)((p[0] << 8) | p[1]); };
    auto u32 = [](const unsigned char* p) {
        return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
    };
    unsigned char kopf[12];
    float faktor = 1.0f;
    // Schriftsammlung (.ttc): erste Schrift der Sammlung nehmen.
    if (std::fread(kopf, 1, 12, f) == 12 && std::memcmp(kopf, "ttcf", 4) == 0) {
        unsigned char o[4];
        if (std::fread(o, 1, 4, f) != 4 || std::fseek(f, (long)u32(o), SEEK_SET) != 0 ||
            std::fread(kopf, 1, 12, f) != 12) {
            std::fclose(f);
            return 1.0f;
        }
    } else {
        std::fseek(f, 12, SEEK_SET);
    }
    {
        const unsigned n = u16(kopf + 4);
        unsigned head = 0, hhea = 0;
        for (unsigned i = 0; i < n && i < 64; ++i) {
            unsigned char e[16];
            if (std::fread(e, 1, 16, f) != 16) break;
            if (std::memcmp(e, "head", 4) == 0) head = u32(e + 8);
            if (std::memcmp(e, "hhea", 4) == 0) hhea = u32(e + 8);
        }
        unsigned char b[8];
        unsigned em = 0;
        int auf = 0, ab = 0;
        if (head && std::fseek(f, (long)head + 18, SEEK_SET) == 0 && std::fread(b, 1, 2, f) == 2) em = u16(b);
        if (hhea && std::fseek(f, (long)hhea + 4, SEEK_SET) == 0 && std::fread(b, 1, 4, f) == 4) {
            auf = (int16_t)u16(b);
            ab = (int16_t)u16(b + 2);
        }
        if (em > 0 && auf - ab > 0) faktor = (float)(auf - ab) / (float)em;
    }
    std::fclose(f);
    return (faktor > 0.5f && faktor < 2.5f) ? faktor : 1.0f;
}

ImFont* Lade(std::initializer_list<const char*> kandidaten, bool symbole) {
    ImGuiIO& io = ImGui::GetIO();
    for (const char* k : kandidaten) {
        const std::string p = Schriftpfad(k);
        if (!Vorhanden(p)) continue;
        ImFontConfig cfg;
        cfg.ExtraSizeScale = EmFaktor(p);
        ImFont* f = io.Fonts->AddFontFromFileTTF(p.c_str(), 13.0f, &cfg);
        if (!f) continue;
        // Haken, Pfeile und Quadrat (✓ ▶ ▼ ▾ ■) fehlen in Segoe UI -- aus
        // Segoe UI Symbol dazumischen.
        if (symbole) {
            const std::string sym = Schriftpfad("seguisym.ttf");
            if (Vorhanden(sym)) {
                ImFontConfig m;
                m.MergeMode = true;
                m.ExtraSizeScale = EmFaktor(sym);
                io.Fonts->AddFontFromFileTTF(sym.c_str(), 13.0f, &m);
            }
            // Japanische Titel: Qt griff dafuer selbst auf eine Systemschrift
            // zurueck. Geladen wird je Zeichen erst bei Bedarf.
            for (const char* cjk : {"YuGothR.ttc", "msgothic.ttc"}) {
                const std::string p2 = Schriftpfad(cjk);
                if (!Vorhanden(p2)) continue;
                ImFontConfig m;
                m.MergeMode = true;
                m.ExtraSizeScale = EmFaktor(p2);
                io.Fonts->AddFontFromFileTTF(p2.c_str(), 13.0f, &m);
                break;
            }
        }
        return f;
    }
    return nullptr;
}

void StilSetzen(float dpi) {
    auto C = [](unsigned rgb, float a = 1.0f) {
        return ImVec4(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, a);
    };
    ImGuiStyle& s = ImGui::GetStyle();
    s.FontSizeBase = 12.0f;   // Qt-Standardschrift unter Windows: Segoe UI 9 pt = 12 px
    s.FontScaleDpi = dpi;
    s.WindowPadding = ImVec2(8, 8);
    s.FramePadding = ImVec2(8, 4);
    s.ItemSpacing = ImVec2(6, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.ScrollbarSize = 12.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.WindowRounding = 4.0f;
    s.ChildRounding = 0.0f;
    s.FrameRounding = 3.0f;
    s.PopupRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.ScrollbarRounding = 6.0f;
    s.ScaleAllSizes(dpi);

    // Farben aus der NES-Palette (colors.h der Qt-Fassung).
    ImVec4* f = s.Colors;
    f[ImGuiCol_Text] = C(0xd7d3cc);
    f[ImGuiCol_TextDisabled] = C(0x8a8580);
    f[ImGuiCol_WindowBg] = C(0x534c4d);
    f[ImGuiCol_ChildBg] = C(0x534c4d);
    f[ImGuiCol_PopupBg] = C(0x3f3a3b);
    f[ImGuiCol_Border] = C(0xa9a49c, 0.55f);
    f[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    f[ImGuiCol_FrameBg] = C(0x2f2b2c);
    f[ImGuiCol_FrameBgHovered] = C(0x3a3536);
    f[ImGuiCol_FrameBgActive] = C(0x45403f);
    f[ImGuiCol_TitleBg] = C(0x3f3a3b);
    f[ImGuiCol_TitleBgActive] = C(0x3f3a3b);
    f[ImGuiCol_TitleBgCollapsed] = C(0x3f3a3b);
    f[ImGuiCol_MenuBarBg] = C(0x3f3a3b);
    f[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    f[ImGuiCol_ScrollbarGrab] = C(0x3f3a3b);
    f[ImGuiCol_ScrollbarGrabHovered] = C(0x2a2726);
    f[ImGuiCol_ScrollbarGrabActive] = C(0xa9a49c);
    f[ImGuiCol_CheckMark] = C(0x6fbf6f);
    f[ImGuiCol_SliderGrab] = C(0xa9a49c);
    f[ImGuiCol_SliderGrabActive] = C(0xd7d3cc);
    // Knoepfe wie im Windows-Stil der Qt-Fassung: etwas dunkler als die
    // Flaeche, mit hellem Rand.
    f[ImGuiCol_Button] = C(0x4a4445);
    f[ImGuiCol_ButtonHovered] = C(0x5a5455);
    f[ImGuiCol_ButtonActive] = C(0x3f3a3b);
    f[ImGuiCol_Header] = C(0x6b6364);
    f[ImGuiCol_HeaderHovered] = C(0x7b7374);
    f[ImGuiCol_HeaderActive] = C(0x5d5657);
    f[ImGuiCol_Separator] = C(0xa9a49c, 0.5f);
    f[ImGuiCol_TextSelectedBg] = C(0x6fbf6f, 0.35f);
    f[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
    f[ImGuiCol_NavCursor] = C(0x6fbf6f);
}

// Fensterrahmen setzen, so dass die Innenflaeche genau `innen` bleibt
// (Qt: setGeometry() bezieht sich auf die Innenflaeche).
void RahmenAnlegen(HWND hwnd, const RECT& innen) {
    const DWORD stil = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    ::SetWindowLongPtrW(hwnd, GWL_STYLE, stil);
    RECT r = innen;
    ::AdjustWindowRectEx(&r, stil, FALSE, 0);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        const RECT& a = mi.rcWork;
        if (r.top < a.top) { const LONG d = a.top - r.top; r.top += d; r.bottom += d; }
        if (r.bottom > a.bottom) r.bottom = a.bottom;
    }
    ::SetWindowPos(hwnd, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                   SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    ::SetUnhandledExceptionFilter(AufAbsturz);
    std::set_terminate(AufTerminate);

    // Systemweite DPI-Kenntnis: Schriften scharf statt hochgerechnet.
    if (HMODULE u = ::GetModuleHandleW(L"user32.dll")) {
        using Fn = BOOL(WINAPI*)(HANDLE);
        if (auto fn = reinterpret_cast<Fn>(::GetProcAddress(u, "SetProcessDpiAwarenessContext")))
            fn(reinterpret_cast<HANDLE>(-2) /* DPI_AWARENESS_CONTEXT_SYSTEM_AWARE */);
        else ::SetProcessDPIAware();
    }
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bilder::Start();

    // Sprache VOR dem Aufbau setzen.
    {
        Einstellungen e;
        e.Laden(plattform::ExeOrdner() + "/rawnes_gui_config.json");
        i18n::SpracheSetzen(e.language);
    }

    HDC bildschirm = ::GetDC(nullptr);
    float dpi = (float)::GetDeviceCaps(bildschirm, LOGPIXELSX) / 96.0f;
    ::ReleaseDC(nullptr, bildschirm);
    if (dpi < 0.5f || dpi > 4.0f) dpi = 1.0f;

    // Intro und Hauptfenster: 760 x 900, mittig im Arbeitsbereich, hoechstens
    // so gross wie dieser (IntroWindow::showBlocking).
    RECT geo;
    {
        RECT a;
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &a, 0);
        const int b = std::min((int)(App::kBreite * dpi), (int)(a.right - a.left));
        const int h = std::min((int)(App::kHoehe * dpi), (int)(a.bottom - a.top));
        geo.left = a.left + ((a.right - a.left) - b) / 2;
        geo.top = a.top + ((a.bottom - a.top) - h) / 2;
        geo.right = geo.left + b;
        geo.bottom = geo.top + h;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)::LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, ::GetSystemMetrics(SM_CXICON),
                                   ::GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)::LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON),
                                     ::GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"RawNesMonitor";
    ::RegisterClassExW(&wc);

    // Intro: rahmenlos und obenauf (Qt::FramelessWindowHint | WindowStaysOnTopHint).
    HWND hwnd = ::CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"RAW-NES Monitor", WS_POPUP,
                                  geo.left, geo.top, geo.right - geo.left, geo.bottom - geo.top,
                                  nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    if (!CreateGlContext(hwnd)) {
        ::MessageBoxW(nullptr, L"OpenGL-Kontext konnte nicht angelegt werden.", L"RAW-NES", MB_ICONERROR);
        ::DestroyWindow(hwnd);
        return 1;
    }
    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);
    // An die Bildwiederholrate koppeln, sonst zeichnet die Schleife ungebremst.
    // Ohne Grafiktreiber (GDI Generic) gibt es das nicht -- dann je Bild kurz warten.
    bool vsync = false;
    {
        using SwapFn = BOOL(WINAPI*)(int);
        if (auto swap = reinterpret_cast<SwapFn>(::wglGetProcAddress("wglSwapIntervalEXT"))) vsync = swap(1) != FALSE;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    Schriften schriften;
    schriften.ui = Lade({"segoeui.ttf", "tahoma.ttf", "arial.ttf"}, true);
    schriften.fett = Lade({"segoeuib.ttf", "arialbd.ttf"}, true);
    schriften.mono = Lade({"consola.ttf", "cour.ttf"}, false);
    schriften.monoFett = Lade({"consolab.ttf", "courbd.ttf"}, false);
    schriften.courierFett = Lade({"courbd.ttf", "consolab.ttf"}, false);
    schriften.courierKursiv = Lade({"couri.ttf", "consolai.ttf"}, false);
    ImFont* standard = ImGui::GetIO().Fonts->AddFontDefault();
    if (!schriften.ui) schriften.ui = standard;
    if (!schriften.fett) schriften.fett = schriften.ui;
    if (!schriften.mono) schriften.mono = schriften.ui;
    if (!schriften.monoFett) schriften.monoFett = schriften.mono;
    ImGui::GetIO().FontDefault = schriften.ui;
    StilSetzen(dpi);

    ImGui_ImplWin32_InitForOpenGL(hwnd);
    ImGui_ImplOpenGL2_Init();

    intro::Laden(schriften.courierFett, schriften.courierKursiv);
    intro::Starten();

    std::unique_ptr<App> app;
    bool imIntro = true;
    ULONGLONG ausblendenAb = 0;
    constexpr ULONGLONG kAusblendMs = 350;  // Ausblenden des Intros

    bool running = true;
    while (running) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;
        if (::IsIconic(hwnd)) { ::Sleep(30); continue; }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (imIntro) {
            if (!intro::Zeichnen(dpi)) {
                // Intro vorbei: Hauptfenster an genau derselben Stelle.
                imIntro = false;
                RahmenAnlegen(hwnd, geo);
                app = std::make_unique<App>(hwnd, dpi, schriften);
                g_app = app.get();
                app->Starten();
                ausblendenAb = ::GetTickCount64();
            }
        } else {
            app->Zeichnen();
            const ULONGLONG lauf = ::GetTickCount64() - ausblendenAb;
            if (lauf < kAusblendMs) {
                const float a = 1.0f - (float)lauf / (float)kAusblendMs;
                const ImGuiViewport* vp = ImGui::GetMainViewport();
                ImGui::GetForegroundDrawList()->AddRectFilled(
                    vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                    IM_COL32(0x05, 0x05, 0x0a, (int)(a * 255)));
            }
        }

        ImGui::Render();
        ::glClearColor(0x53 / 255.0f, 0x4c / 255.0f, 0x4d / 255.0f, 1.0f);
        ::glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        ::SwapBuffers(g_dc);
        if (!vsync) ::Sleep(10);
    }

    // Fenster sofort weg -- das Aufraeumen darunter kann dauern.
    ::ShowWindow(hwnd, SW_HIDE);
    // Letzte Rettung wie in der Qt-Fassung (wait(5000), dann terminate()):
    // Steckt der Worker gerade in einem langen Verbindungsversuch (Bluetooth-
    // Anschluesse brauchen zum Oeffnen mitunter sehr lange), wuerde der
    // Prozess sonst unsichtbar weiterlaufen und die Exe gesperrt halten.
    std::thread([]() {
        ::Sleep(3000);
        // Hart beenden: ExitProcess kann selbst haengen, wenn ein Hintergrund-
        // faden gerade eine Sperre haelt (Anschluss, Netz). Dann liefe der Prozess
        // unsichtbar weiter und hielte den COM-Anschluss belegt.
        ::TerminateProcess(::GetCurrentProcess(), 0);
    }).detach();

    g_app = nullptr;
    app.reset();
    popup::Beenden();
    intro::Freigeben();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyGlContext(hwnd);
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, inst);
    bilder::Ende();
    ::CoUninitialize();
    return 0;
}
