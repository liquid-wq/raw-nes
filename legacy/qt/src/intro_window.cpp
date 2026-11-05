// intro_window.cpp -- 1:1-Port der Choreografie aus rawnes_intro.html.
// Jede Konstante hier hat ein direktes Gegenstueck im dortigen JavaScript;
// die Herkunft steht jeweils im Kommentar.
#include "intro_window.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {

// DUR aus dem HTML. play() wartet zusaetzlich 600ms, bevor die Uhr laeuft.
constexpr double kDurMs = 6200.0;
constexpr double kStartDelayMs = 600.0;
constexpr int kFrameCount = 12;

// Palette exakt aus dem HTML-Canvas.
const QColor kBgBase(0x05, 0x05, 0x0a);   // #05050a
const QColor kTextGreen(0x3a, 0xd1, 0x3a); // #3ad13a
const QColor kTextBright(0xea, 0xff, 0xea); // #eaffea
const QColor kStarColor(0xc8, 0xf0, 0xc8); // #c8f0c8
const QColor kCopyColor(0x4a, 0x7a, 0x4a); // #4a7a4a

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
    QString txt;
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
        int n = static_cast<int>(std::floor((t - 1350.0) / 200.0)) + 1;   // 1..4
        s.txt = "RAW" + QString("NESS").left(std::min(4, n));
        s.cursorPos = -2; return s;
    }
    if (t < 3200.0) { s.txt = "RAWNESS"; s.cursorPos = -2; return s; }
    if (t < 3550.0) { s.txt = "RAWNES";  s.cursorPos = -2; return s; }
    if (t < 4300.0) {
        int steps = static_cast<int>(std::floor((t - 3550.0) / 230.0));   // 0..3
        s.txt = "RAWNES";
        s.cursorPos = 6 - std::min(3, steps);                             // 6->5->4->3
        return s;
    }
    if (t < 4700.0) { s.txt = "RAW-NES"; s.cursorPos = 4; return s; }
    if (t < 5400.0) { s.txt = "RAW-NES"; s.cursorPos = 7; return s; }
    s.txt = "RAW-NES"; s.cursorPos = -1; return s;
}

// Ersatz fuer ctx.shadowBlur: dreifacher Box-Blur auf dem Alpha-Kanal
// kommt einem Gauss-Schein nahe genug und bleibt bei 60fps bezahlbar.
void boxBlurArgb(QImage& img, int radius) {
    if (radius < 1) return;
    const int w = img.width(), h = img.height();
    if (w < 2 || h < 2) return;
    QImage tmp(img.size(), QImage::Format_ARGB32_Premultiplied);

    for (int pass = 0; pass < 3; ++pass) {
        // horizontal
        for (int y = 0; y < h; ++y) {
            const QRgb* src = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(tmp.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int a = 0, r = 0, g = 0, b = 0, n = 0;
                int x0 = std::max(0, x - radius), x1 = std::min(w - 1, x + radius);
                for (int i = x0; i <= x1; ++i) {
                    QRgb c = src[i];
                    a += qAlpha(c); r += qRed(c); g += qGreen(c); b += qBlue(c); ++n;
                }
                dst[x] = qRgba(r / n, g / n, b / n, a / n);
            }
        }
        // vertikal
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                int a = 0, r = 0, g = 0, b = 0, n = 0;
                int y0 = std::max(0, y - radius), y1 = std::min(h - 1, y + radius);
                for (int j = y0; j <= y1; ++j) {
                    QRgb c = reinterpret_cast<const QRgb*>(tmp.constScanLine(j))[x];
                    a += qAlpha(c); r += qRed(c); g += qGreen(c); b += qBlue(c); ++n;
                }
                reinterpret_cast<QRgb*>(img.scanLine(y))[x] = qRgba(r / n, g / n, b / n, a / n);
            }
        }
    }
}

} // namespace

QRect IntroWindow::showBlocking() {
    IntroWindow win;
    win.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    // Fenstergroesse und -position exakt so, wie das Hauptfenster gleich
    // erscheinen wird -- das Intro geht damit nahtlos in die GUI ueber.
    // Auf kleinen Bildschirmen wird auf den verfuegbaren Bereich begrenzt.
    QRect geo(0, 0, kWinW, kWinH);
    if (auto* scr = QApplication::primaryScreen()) {
        const QRect avail = scr->availableGeometry();
        geo.setWidth(std::min(kWinW, avail.width()));
        geo.setHeight(std::min(kWinH, avail.height()));
        geo.moveCenter(avail.center());
    }
    // Geometrie VOR show() setzen, damit startSequence() beim Verteilen der
    // Sterne und beim Platzieren des SKIP-Knopfes die endgueltige Groesse
    // sieht.
    win.setGeometry(geo);

    win.frames_.resize(kFrameCount);
    for (int i = 0; i < kFrameCount; ++i) {
        win.frames_[i] = QPixmap(QString(":/cat/cat%1.png").arg(i, 2, 10, QChar('0')));
    }

    QEventLoop loop;
    win.loop_ = &loop;
    win.show();
    win.startSequence();
    // Watchdog, falls der Timer haengen bleibt.
    QTimer::singleShot(static_cast<int>(kDurMs + kStartDelayMs) + 2000, &loop, &QEventLoop::quit);
    loop.exec();

    auto* fade = new QPropertyAnimation(&win, "windowOpacity");
    fade->setDuration(350);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    QEventLoop fadeLoop;
    QObject::connect(fade, &QPropertyAnimation::finished, &fadeLoop, &QEventLoop::quit);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
    fadeLoop.exec();
    win.close();
    return geo;
}

void IntroWindow::startSequence() {
    // SKIP-Knopf unten rechts, Optik aus dem HTML-Stylesheet.
    skipBtn_ = new QPushButton("SKIP", this);
    skipBtn_->setStyleSheet(
        "QPushButton { background: rgba(58,209,58,30); color: #3ad13a; "
        "border: 1px solid #3ad13a; border-radius: 6px; padding: 8px 16px; "
        "font-family: 'Courier New', monospace; font-size: 13px; }"
        "QPushButton:hover { background: rgba(58,209,58,64); }");
    skipBtn_->adjustSize();
    skipBtn_->move(width() - skipBtn_->width() - 18, height() - skipBtn_->height() - 18);
    connect(skipBtn_, &QPushButton::clicked, this, &IntroWindow::finish);

    // initStars() aus dem HTML: 70 Sterne, z 0.4..1.0, Kantenlaenge 1.2..3.7
    auto* rg = QRandomGenerator::global();
    stars_.clear();
    for (int i = 0; i < 70; ++i) {
        stars_.push_back({rg->generateDouble() * width(),
                          rg->generateDouble() * height(),
                          rg->generateDouble() * 0.6 + 0.4,
                          rg->generateDouble() * 2.5 + 1.2});
    }

    clock_ = new QElapsedTimer();
    timer_ = new QTimer(this);
    timer_->setInterval(16); // ~60fps, entspricht requestAnimationFrame
    connect(timer_, &QTimer::timeout, this, &IntroWindow::tick);

    // play() im HTML wartet 600ms, bevor der erste Frame gezeichnet wird.
    QTimer::singleShot(static_cast<int>(kStartDelayMs), this, [this]() {
        clock_->start();
        running_ = true;
        timer_->start();
    });
}

void IntroWindow::tick() {
    if (clock_->elapsed() >= static_cast<qint64>(kDurMs)) {
        timer_->stop();
        finish();
        return;
    }
    update();
}

void IntroWindow::finish() {
    if (timer_) timer_->stop();
    if (loop_ && loop_->isRunning()) loop_->quit();
}

void IntroWindow::mousePressEvent(QMouseEvent*) { finish(); }
void IntroWindow::keyPressEvent(QKeyEvent*)     { finish(); }

void IntroWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Frames sind Pixel-Art; Nearest-Neighbor haelt sie knackig.
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const double W = width(), H = height();

    // Hintergrund: Basisfarbe + radialer Verlauf (r 80 bis max(W,H)*0.7)
    p.fillRect(rect(), kBgBase);
    QRadialGradient bg(W / 2, H / 2, std::max(W, H) * 0.7, W / 2, H / 2);
    bg.setColorAt(0.0, QColor(18, 30, 18, 153));  // rgba(18,30,18,0.6)
    bg.setColorAt(1.0, QColor(5, 5, 10, 153));
    p.fillRect(rect(), bg);

    // Sterne driften nach links, wrappen an der Kante.
    for (Star& st : stars_) {
        st.x -= st.z * 0.3;
        if (st.x < 0) st.x = W;
        QColor c = kStarColor;
        c.setAlphaF(std::min(1.0, st.z * 0.7 + 0.3));
        p.fillRect(QRectF(st.x, st.y, st.s, st.s), c);
    }

    if (!running_ || !clock_) return;
    const double t = static_cast<double>(clock_->elapsed());

    const double cx = W / 2, cy = H / 2;
    const TextState ti = textForTime(t);
    const int jf = frameForTime(t);

    // Kamera-Shake waehrend des Bruellens, klingt mit rawrP ab.
    auto* rg = QRandomGenerator::global();
    const double shk = ti.rawr ? (1.0 - ti.rawrP) * 14.0 : 0.0;
    const double sx = (rg->generateDouble() - 0.5) * shk;
    const double sy = (rg->generateDouble() - 0.5) * shk * 0.4;

    const double dispH = std::min(H * 0.20, 100.0);
    const double groundY = cy + H * 0.05;

    // Shockwave einmalig ausloesen, sobald Frame 8 (Bruellen) ansteht.
    // Ursprung sitzt auf dem Gesicht, nicht in der Bildmitte.
    if (jf == 8 && shockT_ < 0.0) {
        shockT_ = t;
        shockX_ = cx;
        const FrameMeta& mm = kMeta[8];
        const double sc = dispH / mm.h;
        shockY_ = groundY - (mm.bottom - mm.cy) * sc;
    }
    if (shockT_ >= 0.0) {
        const double rt = (t - shockT_) / 600.0;
        if (rt >= 0.0 && rt < 1.0) {
            const double rad = rt * W * 0.6;
            QRadialGradient grd(shockX_, shockY_, rad, shockX_, shockY_);
            grd.setColorAt(0.0,  QColor(200, 255, 200, 0));
            grd.setColorAt(0.7,  QColor(200, 255, 200, 0));
            grd.setColorAt(0.8,  QColor(200, 255, 200, static_cast<int>((1.0 - rt) * 0.5 * 255)));
            grd.setColorAt(1.0,  QColor(200, 255, 200, 0));
            p.fillRect(rect(), grd);

            QPen ring(QColor(220, 255, 220, static_cast<int>((1.0 - rt) * 255)));
            ring.setWidthF(2.0);
            p.setPen(ring);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(shockX_, shockY_), rad, rad);
        }
    }

    // Katze bodenverankert zeichnen: der Anker ist (cx, bottom) des Frames,
    // nicht seine Mitte -- sonst wuerde sie bei jedem Frame-Wechsel springen.
    if (jf >= 0 && jf < kFrameCount && !frames_[jf].isNull()) {
        const FrameMeta& m = kMeta[jf];
        const double scale = dispH / m.h;
        const double dispW = m.w * scale;
        const double tx = cx + sx, ty = groundY + sy;
        const double dx = tx - m.cx * scale;
        const double dy = ty - m.bottom * scale;
        p.drawPixmap(QRect(qRound(dx), qRound(dy), qRound(dispW), qRound(dispH)),
                     frames_[jf], frames_[jf].rect());
    }

    // ---- Text mit Cursor
    const int ts2 = qRound(std::min(W * 0.06, 56.0));
    QFont f("Courier New");
    f.setBold(true);
    f.setPixelSize(ts2);
    QFontMetricsF fm(f);

    const QString full = ti.txt;
    const QColor baseCol = ti.rawr ? kTextBright : kTextGreen;
    const bool blink = (static_cast<int>(t / 250.0) % 2 == 0);
    const int cpos = ti.cursorPos;

    const double originX = qRound(cx + sx);
    const double originY = qRound(cy + H * 0.19 + sy);
    const double tw = fm.horizontalAdvance(full);

    // Zeichnet Text plus Cursor relativ zum Ursprung -- einmal fuer den
    // Glow in einen Puffer, einmal direkt auf den Bildschirm.
    auto drawText = [&](QPainter& tp, double ox, double oy, const QColor& col) {
        tp.setFont(f);
        double x = ox - tw / 2;
        const double baseline = oy + fm.ascent() / 2.0 - fm.descent() / 2.0; // textBaseline='middle'
        for (int i = 0; i <= full.length(); ++i) {
            if (i == cpos && cpos >= 0 && blink) {
                tp.setPen(col);
                tp.drawText(QPointF(qRound(x - 3), baseline), "|");
            }
            if (i < full.length()) {
                // Bindestrich hebt sich ab, solange er frisch eingefuegt ist.
                tp.setPen((full[i] == '-' && t < 4800.0) ? kTextBright : col);
                tp.drawText(QPointF(qRound(x), baseline), QString(full[i]));
                x += fm.horizontalAdvance(full[i]);
            }
        }
    };

    if (ti.rawr && !full.isEmpty()) {
        // Gegenstueck zu ctx.shadowBlur = 40 mit gruenem Schein.
        const int pad = 48;
        const int bw = static_cast<int>(tw) + pad * 2;
        const int bh = static_cast<int>(fm.height()) + pad * 2;
        if (bw > 0 && bh > 0) {
            QImage glow(bw, bh, QImage::Format_ARGB32_Premultiplied);
            glow.fill(Qt::transparent);
            {
                QPainter gp(&glow);
                gp.setRenderHint(QPainter::Antialiasing, true);
                drawText(gp, bw / 2.0, bh / 2.0, kTextGreen);
            }
            boxBlurArgb(glow, 6);
            p.setOpacity(std::min(1.0, 0.6 + ti.rawrP * 0.4));
            p.drawImage(QPointF(originX - bw / 2.0, originY - bh / 2.0), glow);
            p.setOpacity(1.0);
        }
    }

    drawText(p, originX, originY, baseCol);

    // Copyright
    p.setOpacity(0.5);
    QFont f2("Courier New");
    f2.setItalic(true);
    f2.setPixelSize(qRound(std::min(W * 0.02, 18.0)));
    p.setFont(f2);
    p.setPen(kCopyColor);
    QFontMetricsF fm2(f2);
    const QString sub = QString::fromUtf8("\u00a9 2026 Liqui");
    p.drawText(QPointF(cx - fm2.horizontalAdvance(sub) / 2.0, cy + H * 0.235), sub);
    p.setOpacity(1.0);
}
