#include "seven_segment.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QSizePolicy>
#include <array>
#include <algorithm>
#include <map>

namespace {
// Segmentreihenfolge: a=oben, b=oben-rechts, c=unten-rechts, d=unten,
// e=unten-links, f=oben-links, g=Mitte. 1 = an.
using Segs = std::array<bool, 7>;

const std::map<char, Segs>& segmentTable() {
    static const std::map<char, Segs> table = {
        {'0', {1, 1, 1, 1, 1, 1, 0}},
        {'1', {0, 1, 1, 0, 0, 0, 0}},
        {'2', {1, 1, 0, 1, 1, 0, 1}},
        {'3', {1, 1, 1, 1, 0, 0, 1}},
        {'4', {0, 1, 1, 0, 0, 1, 1}},
        {'5', {1, 0, 1, 1, 0, 1, 1}},
        {'6', {1, 0, 1, 1, 1, 1, 1}},
        {'7', {1, 1, 1, 0, 0, 0, 0}},
        {'8', {1, 1, 1, 1, 1, 1, 1}},
        {'9', {1, 1, 1, 1, 0, 1, 1}},
        {'A', {1, 1, 1, 0, 1, 1, 1}},
        {'B', {0, 0, 1, 1, 1, 1, 1}},
        {'C', {1, 0, 0, 1, 1, 1, 0}},
        {'D', {0, 1, 1, 1, 1, 0, 1}},
        {'E', {1, 0, 0, 1, 1, 1, 1}},
        {'F', {1, 0, 0, 0, 1, 1, 1}},
        {'-', {0, 0, 0, 0, 0, 0, 1}},
        {' ', {0, 0, 0, 0, 0, 0, 0}},
    };
    return table;
}
} // namespace

SevenSegmentDigit::SevenSegmentDigit(QWidget* parent)
    : QWidget(parent), color_(QColor("#40d060")) {
    // Deutlich groesser als vorher (war 18x32) -- die Ziffern sollen das
    // Panel dominieren, nicht darin verschwinden. Expanding, damit sie
    // sich mit dem verfuegbaren Platz mitskalieren statt starr klein zu
    // bleiben, wenn das Fenster breiter ist.
    setMinimumSize(26, 46);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* glow = new QGraphicsDropShadowEffect(this);
    glow->setBlurRadius(34); // kraeftigerer Halo als vorher (war 18)
    glow->setOffset(0, 0);
    glow->setColor(color_);
    setGraphicsEffect(glow);
}

void SevenSegmentDigit::setChar(QChar c) {
    if (ch_ == c) return;
    ch_ = c;
    update();
}

void SevenSegmentDigit::setColor(const QColor& color) {
    color_ = color;
    if (auto* glow = qobject_cast<QGraphicsDropShadowEffect*>(graphicsEffect())) {
        glow->setColor(color_);
    }
    update();
}

QSize SevenSegmentDigit::sizeHint() const { return QSize(36, 66); }

void SevenSegmentDigit::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Schwarzer LED-Panel-Hintergrund pro Ziffer mit ganz leichtem
    // Verlauf (statt platter Flaeche) -- wirkt wie ein Fach mit Tiefe.
    QLinearGradient bgGrad(0, 0, 0, height());
    bgGrad.setColorAt(0.0, QColor(4, 4, 5));
    bgGrad.setColorAt(1.0, QColor(14, 13, 14));
    p.fillRect(rect(), bgGrad);

    char key = ch_.toUpper().toLatin1();
    const auto& table = segmentTable();
    auto it = table.find(key);
    Segs segs = (it != table.end()) ? it->second : Segs{0, 0, 0, 0, 0, 0, 0};

    const double w = width();
    const double h = height();
    const double margin = std::min(w, h) * 0.13;
    const double th = std::min(w, h) * 0.17; // Segmentdicke

    const double x0 = margin;
    const double x1 = w - margin;
    const double y0 = margin;
    const double y1 = h / 2.0;
    const double y2 = h - margin;

    const QRectF segA(x0 + th * 0.5, y0, x1 - x0 - th, th);              // oben
    const QRectF segG(x0 + th * 0.5, y1 - th / 2.0, x1 - x0 - th, th);   // mitte
    const QRectF segD(x0 + th * 0.5, y2 - th, x1 - x0 - th, th);         // unten
    const QRectF segF(x0, y0, th, y1 - y0);                             // oben-links
    const QRectF segB(x1 - th, y0, th, y1 - y0);                        // oben-rechts
    const QRectF segE(x0, y1, th, y2 - y1);                             // unten-links
    const QRectF segC(x1 - th, y1, th, y2 - y1);                        // unten-rechts

    const QRectF* rects[7] = {&segA, &segB, &segC, &segD, &segE, &segF, &segG};
    const double rad = th * 0.28;

    QColor offColor = color_.darker(700); // "Geister"-Segment, wie beim Vorbild
    p.setPen(Qt::NoPen);

    // Unlit-Segmente zuerst (dezenter Ghost-Look).
    for (int i = 0; i < 7; ++i) {
        if (!segs[i]) {
            p.setBrush(offColor);
            p.drawRoundedRect(*rects[i], rad, rad);
        }
    }

    // Lit-Segmente: weicher Halo dahinter + Glas-Verlauf davor + duenner
    // heller Glanzstreifen oben drauf. Das ersetzt die vorherige platte
    // Einfarbflaeche und gibt den "billigen" Look auf.
    for (int i = 0; i < 7; ++i) {
        if (!segs[i]) continue;
        const QRectF& r = *rects[i];

        // 1) weicher radialer Halo, etwas groesser als das Segment.
        QRectF halo = r.adjusted(-th * 0.55, -th * 0.55, th * 0.55, th * 0.55);
        QRadialGradient haloGrad(halo.center(), std::max(halo.width(), halo.height()) / 2.0);
        QColor haloColor = color_;
        haloColor.setAlpha(140);
        haloGrad.setColorAt(0.0, haloColor);
        QColor haloEdge = color_;
        haloEdge.setAlpha(0);
        haloGrad.setColorAt(1.0, haloEdge);
        p.setBrush(haloGrad);
        p.drawRoundedRect(halo, rad * 1.4, rad * 1.4);

        // 2) Kern-Segment mit vertikalem Glasverlauf (hell oben, satte
        // Grundfarbe unten) -- wirkt wie eine gewoelbte LED-Linse statt
        // einer Flachfarbe.
        QLinearGradient glass(r.topLeft(), r.bottomLeft());
        glass.setColorAt(0.0, color_.lighter(170));
        glass.setColorAt(0.45, color_.lighter(110));
        glass.setColorAt(1.0, color_.darker(115));
        p.setBrush(glass);
        p.drawRoundedRect(r, rad, rad);

        // 3) duenner heller Glanzstreifen am oberen Rand des Segments.
        QRectF shine = r.adjusted(r.width() * 0.12, r.height() * 0.12,
                                   -r.width() * 0.12, -r.height() * 0.55);
        if (shine.height() > 0.5 && shine.width() > 0.5) {
            QColor shineColor(255, 255, 255, 90);
            p.setBrush(shineColor);
            p.drawRoundedRect(shine, rad * 0.6, rad * 0.6);
        }
    }
}

LedDigitStrip::LedDigitStrip(int digitCount, QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(3);
    for (int i = 0; i < digitCount; ++i) {
        auto* digit = new SevenSegmentDigit(this);
        digits_.push_back(digit);
        // Stretch=1 pro Ziffer, damit sich alle gleichmaessig mit dem
        // verfuegbaren Platz dehnen statt auf Mindestbreite zu bleiben.
        layout->addWidget(digit, 1);
    }
}

void LedDigitStrip::setText(const QString& text) {
    int n = static_cast<int>(digits_.size());
    QString padded = text;
    if (padded.size() < n) {
        padded = QString(n - padded.size(), QChar(' ')) + padded;
    } else if (padded.size() > n) {
        padded = padded.right(n); // von links abschneiden, rechtsbuendig bleibt sichtbar
    }
    for (int i = 0; i < n; ++i) digits_[i]->setChar(padded.at(i));
}

void LedDigitStrip::setColor(const QColor& color) {
    for (auto* d : digits_) d->setColor(color);
}
