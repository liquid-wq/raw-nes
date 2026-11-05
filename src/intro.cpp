#include "intro.h"

#include "bilder.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace intro {
namespace {

// DUR aus dem HTML. play() wartet zusaetzlich 600ms, bevor die Uhr laeuft.
constexpr double kDurMs = 6200.0;
constexpr double kStartDelayMs = 600.0;
constexpr int kFrameCount = 12;
constexpr int kFrameRessource = 200;  // cat00.png .. cat11.png, siehe app.rc.in

// Palette exakt aus dem HTML-Canvas.
constexpr ImU32 kBgBase = IM_COL32(0x05, 0x05, 0x0a, 255);
constexpr ImU32 kTextGreen = IM_COL32(0x3a, 0xd1, 0x3a, 255);
constexpr ImU32 kTextBright = IM_COL32(0xea, 0xff, 0xea, 255);
constexpr ImU32 kCopyColor = IM_COL32(0x4a, 0x7a, 0x4a, 255);

// FMETA aus rawnes_intro.html. bottom/cx sind der Bodenanker je Frame.
struct FrameMeta { int w, h; double cx, cy; double bottom; };
const FrameMeta kMeta[kFrameCount] = {
    {158, 149,  74.0,  73.5, 144},  // reinspringen1
    {256, 159, 150.4,  84.0, 154},  // reinspringen2
    {197, 169,  97.9,  87.5, 164},  // sprung_luft
    {233, 143, 126.4,  69.6, 138},  // landung1
    {194, 158,  99.7,  81.6, 153},  // landung2
    {208, 184, 116.1,  91.7, 179},  // nach_vorne1
    {201, 183, 112.9,  91.6, 178},  // nach_vorne2
    {146, 186,  72.5,  95.5, 181},  // vorbereiten
    {181, 193,  71.0,  98.8, 188},  // miau
    {144, 199,  71.8, 103.9, 194},  // hinsetzen1
    {143, 200,  68.4, 107.0, 196},  // hinsetzen2
    {144, 200,  69.5, 107.2, 195},  // endpose
};

// KF aus dem HTML: Katzen-Keyframes auf derselben Uhr wie der Text.
struct KeyFrame { double t; int f; };
const KeyFrame kKeyFrames[] = {
    {0.0,    11},  // sitzt praesent
    {800.0,   8},  // BRUELLEN beginnt mit dem R
    {1900.0,  9},  // nach "RAW" -> setzt sich zurueck
    {2200.0, 10},
    {2500.0, 11},  // Endpose, bleibt sitzen
};

int frameForTime(double t) {
    int f = kKeyFrames[0].f;
    for (const auto& kf : kKeyFrames) {
        if (t >= kf.t) f = kf.f;
    }
    return f;
}

// textForTime() aus dem HTML, unveraendert uebernommen.
// cursorPos < 0 bedeutet: kein Cursor (im Original -1 bzw. -2).
struct TextState {
    std::string txt;
    int cursorPos = -1;
    bool rawr = false;
    double rawrP = 0.0;
};

TextState textForTime(double t) {
    TextState s;
    if (t < 800.0)  { s.txt = "";     s.cursorPos = 0; return s; }
    if (t < 975.0)  { s.txt = "R";    s.rawr = true; s.rawrP = (t - 800.0) / 400.0; s.cursorPos = -2; return s; }
    if (t < 1150.0) { s.txt = "RA";   s.rawr = true; s.rawrP = (t - 800.0) / 400.0; s.cursorPos = -2; return s; }
    if (t < 1350.0) { s.txt = "RAW";  s.rawr = true;
                      s.rawrP = std::min(1.0, (t - 800.0) / 550.0); s.cursorPos = -2; return s; }
    if (t < 2150.0) {
        const int n = static_cast<int>(std::floor((t - 1350.0) / 200.0)) + 1;  // 1..4
        s.txt = "RAW" + std::string("NESS").substr(0, (size_t)std::min(4, n));
        s.cursorPos = -2; return s;
    }
    if (t < 3200.0) { s.txt = "RAWNESS"; s.cursorPos = -2; return s; }
    if (t < 3550.0) { s.txt = "RAWNES";  s.cursorPos = -2; return s; }
    if (t < 4300.0) {
        const int steps = static_cast<int>(std::floor((t - 3550.0) / 230.0));  // 0..3
        s.txt = "RAWNES";
        s.cursorPos = 6 - std::min(3, steps);                                // 6->5->4->3
        return s;
    }
    if (t < 4700.0) { s.txt = "RAW-NES"; s.cursorPos = 4; return s; }
    if (t < 5400.0) { s.txt = "RAW-NES"; s.cursorPos = 7; return s; }
    s.txt = "RAW-NES"; s.cursorPos = -1; return s;
}

struct Star { double x, y, z, s; };

ImFont* g_fett = nullptr;
ImFont* g_kursiv = nullptr;
bilder::Textur g_frames[kFrameCount];
bilder::Textur g_verlauf;   // radialer Verlauf des Hintergrunds
bool g_geladen = false;

std::vector<Star> g_stars;
std::mt19937 g_zufall;
ULONGLONG g_beginn = 0;     // Start inkl. Vorlauf
ULONGLONG g_letztes = 0;
double g_shockT = -1.0;
double g_shockX = 0.0, g_shockY = 0.0;
bool g_sterneVerteilt = false;
bool g_fertig = false;

double Zufall() { return std::uniform_real_distribution<double>(0.0, 1.0)(g_zufall); }

ImU32 MitAlpha(ImU32 c, double a) {
    const int alpha = (int)std::lround(std::max(0.0, std::min(1.0, a)) * ((c >> IM_COL32_A_SHIFT) & 0xFF));
    return (c & ~IM_COL32_A_MASK) | ((ImU32)alpha << IM_COL32_A_SHIFT);
}

// Radialer Verlauf rgba(18,30,18,0.6) -> rgba(5,5,10,0.6) als kleine Textur;
// die Zeichenliste von ImGui kennt keine radialen Verlaeufe.
void VerlaufErzeugen() {
    const int n = 64;
    std::vector<uint8_t> px((size_t)n * n * 4);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const double dx = (x + 0.5) / n * 2.0 - 1.0, dy = (y + 0.5) / n * 2.0 - 1.0;
            const double t = std::min(1.0, std::sqrt(dx * dx + dy * dy));
            uint8_t* p = &px[((size_t)y * n + x) * 4];
            p[0] = (uint8_t)std::lround(18 + (5 - 18) * t);
            p[1] = (uint8_t)std::lround(30 + (5 - 30) * t);
            p[2] = (uint8_t)std::lround(18 + (10 - 18) * t);
            p[3] = 153;
        }
    }
    g_verlauf = bilder::Hochladen(px.data(), n, n, false);
}

}  // namespace

void Laden(ImFont* fett, ImFont* kursiv) {
    g_fett = fett;
    g_kursiv = kursiv;
    if (g_geladen) return;
    for (int i = 0; i < kFrameCount; ++i) {
        const void* d = nullptr;
        size_t n = 0;
        // Frames sind Pixel-Art; Nearest-Neighbor haelt sie knackig.
        if (bilder::Ressource(kFrameRessource + i, &d, &n)) g_frames[i] = bilder::PngHochladen(d, n, true);
    }
    VerlaufErzeugen();
    g_geladen = true;
}

void Starten() {
    g_zufall.seed((unsigned)::GetTickCount());
    g_beginn = ::GetTickCount64();
    g_letztes = g_beginn;
    g_shockT = -1.0;
    g_sterneVerteilt = false;
    g_fertig = false;
}

bool Zeichnen(float skalierung) {
    if (g_fertig) return false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float W = vp->Size.x, H = vp->Size.y;
    const ImVec2 o = vp->Pos;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    const ULONGLONG jetzt = ::GetTickCount64();
    const double seitBeginn = (double)(jetzt - g_beginn);
    const double dt = (double)(jetzt - g_letztes);
    g_letztes = jetzt;

    // initStars() aus dem HTML: 70 Sterne, z 0.4..1.0, Kantenlaenge 1.2..3.7
    if (!g_sterneVerteilt) {
        g_stars.clear();
        for (int i = 0; i < 70; ++i)
            g_stars.push_back({Zufall() * W, Zufall() * H, Zufall() * 0.6 + 0.4, Zufall() * 2.5 + 1.2});
        g_sterneVerteilt = true;
    }

    // Hintergrund: Basisfarbe + radialer Verlauf (r bis max(W,H)*0.7)
    dl->AddRectFilled(o, ImVec2(o.x + W, o.y + H), kBgBase);
    if (g_verlauf.ok()) {
        const float R = std::max(W, H) * 0.7f;
        const ImVec2 c(o.x + W / 2, o.y + H / 2);
        dl->AddRectFilled(o, ImVec2(o.x + W, o.y + H), IM_COL32(5, 5, 10, 153));
        dl->AddImage((ImTextureID)(intptr_t)g_verlauf.id, ImVec2(c.x - R, c.y - R), ImVec2(c.x + R, c.y + R),
                     ImVec2(0, 0), ImVec2(g_verlauf.u, g_verlauf.v));
    }

    // Sterne driften nach links, wrappen an der Kante. Im Original je Bild
    // (16 ms) um z*0.3 -- hier auf die tatsaechliche Bildzeit umgerechnet.
    for (Star& st : g_stars) {
        st.x -= st.z * 0.3 * (dt / 16.0);
        if (st.x < 0) st.x = W;
        const double a = std::min(1.0, st.z * 0.7 + 0.3);
        dl->AddRectFilled(ImVec2(o.x + (float)st.x, o.y + (float)st.y),
                          ImVec2(o.x + (float)(st.x + st.s), o.y + (float)(st.y + st.s)),
                          IM_COL32(0xc8, 0xf0, 0xc8, (int)(a * 255)));
    }

    // SKIP-Knopf unten rechts, Optik aus dem HTML-Stylesheet.
    bool ueberspringen = false;
    {
        const char* skip = "SKIP";
        const float gr = 13.0f * skalierung;
        ImFont* f = g_fett ? g_fett : ImGui::GetFont();
        const ImVec2 ts = f->CalcTextSizeA(gr, FLT_MAX, 0.0f, skip);
        const ImVec2 groesse(ts.x + 32.0f * skalierung, ts.y + 16.0f * skalierung);
        const ImVec2 p0(o.x + W - groesse.x - 18.0f * skalierung, o.y + H - groesse.y - 18.0f * skalierung);
        const ImVec2 p1(p0.x + groesse.x, p0.y + groesse.y);
        const bool ueber = ImGui::IsMouseHoveringRect(p0, p1, false);
        dl->AddRectFilled(p0, p1, IM_COL32(58, 209, 58, ueber ? 64 : 30), 6.0f * skalierung);
        dl->AddRect(p0, p1, kTextGreen, 6.0f * skalierung, 0, 1.0f);
        dl->AddText(f, gr, ImVec2(p0.x + 16.0f * skalierung, p0.y + 8.0f * skalierung), kTextGreen, skip);
    }
    // Wie mousePressEvent/keyPressEvent im Original: jeder Klick, jede Taste.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ueberspringen = true;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !ueberspringen; ++k) {
        if (k >= ImGuiKey_MouseLeft && k <= ImGuiKey_MouseWheelY) continue;
        if (ImGui::IsKeyPressed((ImGuiKey)k, false)) ueberspringen = true;
    }
    if (ueberspringen) { g_fertig = true; return false; }

    if (seitBeginn < kStartDelayMs) return true;
    const double t = seitBeginn - kStartDelayMs;
    if (t >= kDurMs) { g_fertig = true; return false; }

    const double cx = W / 2.0, cy = H / 2.0;
    const TextState ti = textForTime(t);
    const int jf = frameForTime(t);

    // Kamera-Shake waehrend des Bruellens, klingt mit rawrP ab.
    const double shk = ti.rawr ? (1.0 - ti.rawrP) * 14.0 : 0.0;
    const double sx = (Zufall() - 0.5) * shk;
    const double sy = (Zufall() - 0.5) * shk * 0.4;

    const double dispH = std::min(H * 0.20, 100.0 * skalierung);
    const double groundY = cy + H * 0.05;

    // Shockwave einmalig, sobald Frame 8 (Bruellen) ansteht; Ursprung auf
    // dem Gesicht, nicht in der Bildmitte.
    if (jf == 8 && g_shockT < 0.0) {
        g_shockT = t;
        g_shockX = cx;
        const FrameMeta& mm = kMeta[8];
        const double sc = dispH / mm.h;
        g_shockY = groundY - (mm.bottom - mm.cy) * sc;
    }
    if (g_shockT >= 0.0) {
        const double rt = (t - g_shockT) / 600.0;
        if (rt >= 0.0 && rt < 1.0) {
            const double rad = rt * W * 0.6;
            const ImVec2 m(o.x + (float)g_shockX, o.y + (float)g_shockY);
            // Leuchtband zwischen 70 % und 100 % des Radius, am hellsten bei
            // 80 % -- angenaehert durch konzentrische Ringe.
            const int ringe = 14;
            for (int i = 0; i < ringe; ++i) {
                const double q = 0.7 + 0.3 * (i + 0.5) / ringe;
                const double spitze = q <= 0.8 ? (q - 0.7) / 0.1 : (1.0 - q) / 0.2;
                const double a = (1.0 - rt) * 0.5 * spitze;
                dl->AddCircle(m, (float)(rad * q), IM_COL32(200, 255, 200, (int)(a * 255)), 64,
                              (float)(rad * 0.3 / ringe) + 1.0f);
            }
            dl->AddCircle(m, (float)rad, IM_COL32(220, 255, 220, (int)((1.0 - rt) * 255)), 96, 2.0f);
        }
    }

    // Katze bodenverankert: Anker ist (cx, bottom) des Frames.
    if (jf >= 0 && jf < kFrameCount && g_frames[jf].ok()) {
        const FrameMeta& m = kMeta[jf];
        const double scale = dispH / m.h;
        const double dispW = m.w * scale;
        const double tx = cx + sx, ty = groundY + sy;
        const double dx = std::round(tx - m.cx * scale);
        const double dy = std::round(ty - m.bottom * scale);
        dl->AddImage((ImTextureID)(intptr_t)g_frames[jf].id,
                     ImVec2(o.x + (float)dx, o.y + (float)dy),
                     ImVec2(o.x + (float)(dx + std::round(dispW)), o.y + (float)(dy + std::round(dispH))),
                     ImVec2(0, 0), ImVec2(g_frames[jf].u, g_frames[jf].v));
    }

    // ---- Text mit Cursor
    ImFont* f = g_fett ? g_fett : ImGui::GetFont();
    const float ts2 = (float)std::round(std::min(W * 0.06, 56.0 * skalierung));
    ImFontBaked* baked = f->GetFontBaked(ts2);
    const float ascent = baked ? baked->Ascent : ts2 * 0.8f;
    const float descent = baked ? -baked->Descent : ts2 * 0.2f;

    const std::string& full = ti.txt;
    const ImU32 baseCol = ti.rawr ? kTextBright : kTextGreen;
    const bool blink = (static_cast<int>(t / 250.0) % 2 == 0);
    const int cpos = ti.cursorPos;

    const double originX = std::round(cx + sx);
    const double originY = std::round(cy + H * 0.19 + sy);
    const float tw = f->CalcTextSizeA(ts2, FLT_MAX, 0.0f, full.c_str()).x;
    // textBaseline='middle' -> Oberkante des Textes
    const float oben = (float)originY - (ascent + descent) / 2.0f;

    auto drawText = [&](float ox, float oy, ImU32 col) {
        float x = ox - tw / 2;
        for (int i = 0; i <= (int)full.size(); ++i) {
            if (i == cpos && cpos >= 0 && blink) {
                dl->AddText(f, ts2, ImVec2(o.x + std::round(x - 3), o.y + oy), col, "|");
            }
            if (i < (int)full.size()) {
                const char zeichen[2] = {full[(size_t)i], 0};
                // Bindestrich hebt sich ab, solange er frisch eingefuegt ist.
                const ImU32 c = (full[(size_t)i] == '-' && t < 4800.0) ? MitAlpha(kTextBright, ((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0) : col;
                dl->AddText(f, ts2, ImVec2(o.x + std::round(x), o.y + oy), c, zeichen);
                x += f->CalcTextSizeA(ts2, FLT_MAX, 0.0f, zeichen).x;
            }
        }
    };

    if (ti.rawr && !full.empty()) {
        // Gegenstueck zu ctx.shadowBlur = 40 mit gruenem Schein: der Text
        // mehrfach leicht versetzt und schwach deckend.
        const double deck = std::min(1.0, 0.6 + ti.rawrP * 0.4);
        const float radien[3] = {2.5f, 5.0f, 8.0f};
        for (float r : radien) {
            for (int k = 0; k < 8; ++k) {
                const double w = k * 3.14159265358979 / 4.0;
                drawText((float)originX + (float)std::cos(w) * r * skalierung,
                         oben + (float)std::sin(w) * r * skalierung,
                         MitAlpha(kTextGreen, 0.09 * deck));
            }
        }
    }

    drawText((float)originX, oben, baseCol);

    // Copyright
    {
        ImFont* f2 = g_kursiv ? g_kursiv : ImGui::GetFont();
        const float gr = (float)std::round(std::min(W * 0.02, 18.0 * skalierung));
        const char* sub = "\xC2\xA9 2026 Liqui";
        const float sw = f2->CalcTextSizeA(gr, FLT_MAX, 0.0f, sub).x;
        ImFontBaked* b2 = f2->GetFontBaked(gr);
        const float asc2 = b2 ? b2->Ascent : gr * 0.8f;
        dl->AddText(f2, gr, ImVec2(o.x + (float)(cx - sw / 2.0), o.y + (float)(cy + H * 0.235) - asc2),
                    MitAlpha(kCopyColor, 0.5), sub);
    }
    return true;
}

void Freigeben() {
    for (auto& t : g_frames) bilder::Freigeben(t);
    bilder::Freigeben(g_verlauf);
    g_geladen = false;
}

}  // namespace intro
