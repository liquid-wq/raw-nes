#pragma once
#include <QColor>
#include <QEasingCurve>
#include <QFont>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QScreen>
#include <QSequentialAnimationGroup>
#include <QTimer>
#include <QWidget>
#include <QtMath>
#include <vector>

// Einblendendes Popup bei freigeschaltetem Achievement: schiebt sich von
// rechts herein, federt kurz nach, laesst einen Glanz ueber die Karte
// laufen, bleibt stehen und verschwindet wieder.
//
// Bewusst HEADER-ONLY und OHNE Q_OBJECT:
//  - keine eigenen Signals/Slots noetig (Timer/Animationen verbinden sich
//    auf QTimer bzw. QPropertyAnimation, nicht auf dieses Widget),
//  - dadurch braucht es keinen moc-Lauf und KEINEN Eintrag in
//    CMakeLists.txt. Ein einfaches #include genuegt.
//
// Statt tr() wird rawnesText() aus i18n.h benutzt -- tr() setzt Q_OBJECT
// voraus, das wir hier gerade vermeiden wollen.
#include "i18n.h"

class AchievementPopup : public QWidget {
public:
    explicit AchievementPopup(QWidget* parent = nullptr) : QWidget(nullptr) {
        Q_UNUSED(parent);
        setWindowFlags(Qt::FramelessWindowHint | Qt::Tool |
                       Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFixedSize(kW, kH);

        // Funken, die beim Erscheinen nach aussen stieben.
        auto* rg = QRandomGenerator::global();
        for (int i = 0; i < 34; ++i) {
            Funke f;
            f.winkel = rg->generateDouble() * 2.0 * M_PI;
            f.tempo = rg->generateDouble() * 2.2 + 0.6;
            f.groesse = rg->generateDouble() * 2.2 + 1.0;
            f.phase = rg->generateDouble();
            funken_.push_back(f);
        }

        takt_ = new QTimer(this);
        takt_->setInterval(16); // ~60fps
        QObject::connect(takt_, &QTimer::timeout, this, [this]() {
            zeit_ += 16.0;
            if (zeit_ >= kGesamtMs) { takt_->stop(); hide(); return; }
            update();
        });
    }

    // Zeigt das Popup. badge darf leer sein -- dann wird ein Pokal gezeichnet.
    void zeige(const QString& titel, const QString& beschreibung, int punkte,
               const QPixmap& badge, bool englisch) {
        titel_ = titel;
        beschreibung_ = beschreibung;
        punkte_ = punkte;
        badge_ = badge;
        englisch_ = englisch;
        zeit_ = 0.0;

        // Unten rechts auf dem Bildschirm, mit Rand.
        if (auto* scr = QGuiApplication::primaryScreen()) {
            const QRect a = scr->availableGeometry();
            move(a.right() - kW - 24, a.bottom() - kH - 24);
        }
        show();
        raise();
        takt_->start();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // --- Zeitachse: Slide-In -> Bounce -> Standzeit -> Slide-Out
        const double t = zeit_;
        double vor = 0.0;   // 0 = ganz draussen rechts, 1 = an Position
        double deckkraft = 1.0;
        if (t < kEinMs) {
            QEasingCurve c(QEasingCurve::OutBack); // erzeugt das Nachfedern
            vor = c.valueForProgress(t / kEinMs);
        } else if (t < kEinMs + kStehMs) {
            vor = 1.0;
        } else {
            const double q = (t - kEinMs - kStehMs) / kAusMs;
            QEasingCurve c(QEasingCurve::InCubic);
            vor = 1.0 - c.valueForProgress(qMin(1.0, q));
            deckkraft = 1.0 - qMin(1.0, q);
        }
        const double dx = (1.0 - vor) * (kKarteW + 40);
        p.setOpacity(qBound(0.0, deckkraft, 1.0));
        p.translate(dx, 0);

        // Karte um kReserve nach rechts eingerueckt -- die Bildschirmposition
        // bleibt dadurch exakt dieselbe wie vorher (move() zieht kW ab).
        const QRectF karte(kReserve + 6, 6, kKarteW - 12, kH - 12);

        // --- Funken (nur waehrend des Erscheinens)
        if (t < kEinMs + 500.0) {
            const double leben = qMin(1.0, t / (kEinMs + 500.0));
            p.save();
            p.setPen(Qt::NoPen);
            for (const Funke& f : funken_) {
                const double r = leben * 120.0 * f.tempo;
                const double px = karte.center().x() - 120 + std::cos(f.winkel) * r;
                const double py = karte.center().y() + std::sin(f.winkel) * r * 0.6;
                QColor c(255, 200, 60);
                c.setAlphaF(qBound(0.0, (1.0 - leben) * (0.5 + f.phase * 0.5), 1.0));
                p.setBrush(c);
                p.drawEllipse(QPointF(px, py), f.groesse, f.groesse);
            }
            p.restore();
        }

        // --- Karte
        QPainterPath pfad;
        pfad.addRoundedRect(karte, 14, 14);
        QLinearGradient bg(karte.topLeft(), karte.bottomRight());
        bg.setColorAt(0.0, QColor(0x1c, 0x20, 0x28));
        bg.setColorAt(1.0, QColor(0x0d, 0x10, 0x16));
        p.fillPath(pfad, bg);
        QPen rand(QColor(0xf5, 0xc2, 0x42));
        rand.setWidthF(2.0);
        p.setPen(rand);
        p.drawPath(pfad);

        // --- Glanz, der einmal ueber die Karte wandert
        if (t > 260.0 && t < 1500.0) {
            const double g = (t - 260.0) / 1240.0;
            const double gx = karte.left() + g * karte.width() * 1.5 - karte.width() * 0.25;
            QLinearGradient sh(gx - 60, 0, gx + 60, 0);
            sh.setColorAt(0.0, QColor(255, 255, 255, 0));
            sh.setColorAt(0.5, QColor(255, 245, 200, 46));
            sh.setColorAt(1.0, QColor(255, 255, 255, 0));
            p.save();
            p.setClipPath(pfad);
            p.fillRect(karte, sh);
            p.restore();
        }

        // --- Badge links, pulsiert leicht
        const double puls = 1.0 + 0.05 * std::sin(t / 220.0);
        const double bs = 68.0 * puls;
        const QPointF bm(karte.left() + 56, karte.center().y());
        QRectF bRect(bm.x() - bs / 2, bm.y() - bs / 2, bs, bs);

        QRadialGradient sch(bm, bs * 0.9);
        sch.setColorAt(0.0, QColor(0xf5, 0xc2, 0x42, 90));
        sch.setColorAt(1.0, QColor(0xf5, 0xc2, 0x42, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(sch);
        p.drawEllipse(bm, bs * 0.9, bs * 0.9);

        if (!badge_.isNull()) {
            p.save();
            QPainterPath kreis;
            kreis.addEllipse(bRect);
            p.setClipPath(kreis);
            p.drawPixmap(bRect.toRect(), badge_);
            p.restore();
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0xf5, 0xc2, 0x42), 2.0));
            p.drawEllipse(bRect);
        } else {
            zeichnePokal(p, bRect);
        }

        // --- Texte
        const int lx = static_cast<int>(karte.left()) + 108;
        const int lw = static_cast<int>(karte.width()) - 122;

        QFont f1 = font(); f1.setPointSize(9); f1.setBold(true);
        p.setFont(f1);
        p.setPen(QColor(0xf5, 0xc2, 0x42));
        p.drawText(QRect(lx, static_cast<int>(karte.top()) + 16, lw, 16),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   rawnesText("ACHIEVEMENT FREIGESCHALTET!", englisch_));

        QFont f2 = font(); f2.setPointSize(14); f2.setBold(true);
        p.setFont(f2);
        p.setPen(QColor(0xff, 0xff, 0xff));
        QFontMetrics fm2(f2);
        p.drawText(QRect(lx, static_cast<int>(karte.top()) + 36, lw, 26),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   fm2.elidedText(titel_, Qt::ElideRight, lw));

        QFont f3 = font(); f3.setPointSize(9);
        p.setFont(f3);
        p.setPen(QColor(0xb8, 0xc0, 0xcc));
        QFontMetrics fm3(f3);
        p.drawText(QRect(lx, static_cast<int>(karte.top()) + 64, lw, 18),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   fm3.elidedText(beschreibung_, Qt::ElideRight, lw));

        // --- Punkteleiste: laeuft waehrend der Standzeit voll
        const QRectF leiste(lx, karte.bottom() - 26, lw, 10);
        QPainterPath lp;
        lp.addRoundedRect(leiste, 5, 5);
        p.fillPath(lp, QColor(0x2a, 0x2f, 0x38));
        double fuell = qBound(0.0, (t - kEinMs) / 900.0, 1.0);
        if (fuell > 0.0) {
            QRectF voll(leiste.left(), leiste.top(), leiste.width() * fuell, leiste.height());
            QPainterPath vp;
            vp.addRoundedRect(voll, 5, 5);
            QLinearGradient lg(voll.topLeft(), voll.topRight());
            lg.setColorAt(0.0, QColor(0xf5, 0xc2, 0x42));
            lg.setColorAt(1.0, QColor(0xff, 0xe6, 0x9a));
            p.fillPath(vp, lg);
        }
        if (punkte_ > 0) {
            QFont f4 = font(); f4.setPointSize(8); f4.setBold(true);
            p.setFont(f4);
            p.setPen(QColor(0xf5, 0xc2, 0x42));
            p.drawText(QRect(lx, static_cast<int>(leiste.top()) - 16, lw, 14),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString("%1 %2").arg(punkte_).arg(englisch_ ? "pts" : "Pkt"));
        }
    }

private:
    struct Funke { double winkel, tempo, groesse, phase; };

    // Ersatzgrafik, wenn kein Badge geladen werden konnte.
    void zeichnePokal(QPainter& p, const QRectF& r) {
        p.save();
        p.setBrush(QColor(0x14, 0x18, 0x1e));
        p.setPen(QPen(QColor(0xf5, 0xc2, 0x42), 2.0));
        p.drawEllipse(r);
        QColor gold(0xf5, 0xc2, 0x42);
        p.setPen(Qt::NoPen);
        p.setBrush(gold);
        const double w = r.width(), h = r.height();
        QRectF kelch(r.left() + w * 0.32, r.top() + h * 0.24, w * 0.36, h * 0.30);
        QPainterPath k;
        k.moveTo(kelch.topLeft());
        k.lineTo(kelch.topRight());
        k.lineTo(kelch.center().x() + kelch.width() * 0.18, kelch.bottom());
        k.lineTo(kelch.center().x() - kelch.width() * 0.18, kelch.bottom());
        k.closeSubpath();
        p.drawPath(k);
        p.drawRect(QRectF(r.center().x() - w * 0.03, r.top() + h * 0.54, w * 0.06, h * 0.10));
        p.drawRoundedRect(QRectF(r.left() + w * 0.34, r.top() + h * 0.64, w * 0.32, h * 0.07), 2, 2);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(gold, 2.0));
        p.drawArc(QRectF(r.left() + w * 0.20, r.top() + h * 0.24, w * 0.16, h * 0.20), 90 * 16, 180 * 16);
        p.drawArc(QRectF(r.left() + w * 0.64, r.top() + h * 0.24, w * 0.16, h * 0.20), -90 * 16, 180 * 16);
        p.restore();
    }

    // kKarteW ist die sichtbare Karte. kReserve ist unsichtbarer Platz LINKS
    // davon: die OutBack-Kurve schiesst bis vor=1.10 ueber, das entspricht
    // dx = -46 px bei t~360 ms. Ohne diese Reserve wird das Nachfedern an der
    // Widget-Kante abgeschnitten -- die Karte "springt" sichtbar heraus.
    static constexpr int kKarteW = 420;
    static constexpr int kReserve = 56;
    static constexpr int kW = kKarteW + kReserve;
    static constexpr int kH = 132;
    static constexpr double kEinMs = 620.0;
    static constexpr double kStehMs = 4200.0;
    static constexpr double kAusMs = 480.0;
    static constexpr double kGesamtMs = kEinMs + kStehMs + kAusMs;

    QTimer* takt_ = nullptr;
    double zeit_ = 0.0;
    QString titel_, beschreibung_;
    int punkte_ = 0;
    QPixmap badge_;
    bool englisch_ = false;
    std::vector<Funke> funken_;
};
