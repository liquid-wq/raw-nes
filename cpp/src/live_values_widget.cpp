#include "live_values_widget.h"
#include "colors.h"

#include <QLabel>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// Ruhezustand wie MEGA-RAW. Frisch geaenderte Werte leuchten gold auf und
// glimmen ueber 1s zum Ruhe-Gruen zurueck (frueher: rot, 1s halten + 1s
// Verlauf = 2s).
const QString kRestGreen = "#3dd68c";
constexpr int kGoldR = 0xff, kGoldG = 0xc8, kGoldB = 0x3d;
constexpr int kRestR = 0x3d, kRestG = 0xd6, kRestB = 0x8c;
constexpr qint64 kFadeMs = 1000;
constexpr int kSpalten = 3;

QString colorForAge(qint64 lastMs, qint64 now) {
    if (lastMs <= 0) return kRestGreen;
    qint64 age = now - lastMs;
    if (age >= kFadeMs) return kRestGreen;
    double t = static_cast<double>(age) / static_cast<double>(kFadeMs);
    int r = static_cast<int>(kGoldR + t * (kRestR - kGoldR));
    int g = static_cast<int>(kGoldG + t * (kRestG - kGoldG));
    int b = static_cast<int>(kGoldB + t * (kRestB - kGoldB));
    return QString("#%1%2%3")
        .arg(r, 2, 16, QChar('0')).arg(g, 2, 16, QChar('0')).arg(b, 2, 16, QChar('0'));
}
} // namespace

LiveValuesWidget::LiveValuesWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    label_ = new QLabel(tr("RAM: -"));
    label_->setTextFormat(Qt::RichText);
    label_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    label_->setStyleSheet("background: transparent; color: #3dd68c; padding: 4px 8px;");

    // Rahmen/Hintergrund sitzen am Scrollbereich, nicht am Label -- sonst
    // waere der Rand mitgescrollt.
    scroll_ = new QScrollArea(this);
    scroll_->setWidget(label_);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Hoehe fuer vier Zeilen bei 11pt und 165% Zeilenabstand, plus
    // Innenabstand und Rahmen. Alles darueber hinaus wird gescrollt.
    scroll_->setFixedHeight(112);
    scroll_->setStyleSheet(
        "QScrollArea { background-color: #0a0e14; border: 1px solid #2a3a4a; "
        "border-radius: 4px; }"
        "QScrollArea > QWidget > QWidget { background-color: #0a0e14; }"
        "QScrollBar:vertical { background: #0a0e14; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #2a3a4a; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    layout->addWidget(scroll_);

    fadeTimer_ = new QTimer(this);
    fadeTimer_->setInterval(100);
    connect(fadeTimer_, &QTimer::timeout, this, &LiveValuesWidget::rebuildHtml);
}

void LiveValuesWidget::clearAll() {
    rows_.clear();
    label_->setText(tr("RAM: -"));
    fadeTimer_->stop();
}

void LiveValuesWidget::addAddress(int slot, uint16_t addr) {
    Row r;
    r.addr = addr;
    rows_[slot] = r;
    rebuildHtml();
}

void LiveValuesWidget::updateValue(int slot, uint8_t value, int changeCount) {
    auto it = rows_.find(slot);
    if (it == rows_.end()) return;
    Row& row = it->second;
    if (row.value != value && row.value >= 0) {
        row.lastChangeMs = QDateTime::currentMSecsSinceEpoch();
    }
    row.value = value;
    row.changeCount = changeCount;
    rebuildHtml();
}

void LiveValuesWidget::updateFadeTimer() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool glimmt = false;
    for (const auto& [slot, row] : rows_) {
        if (row.lastChangeMs > 0 && (now - row.lastChangeMs) < kFadeMs) {
            glimmt = true;
            break;
        }
    }
    if (glimmt && !fadeTimer_->isActive()) fadeTimer_->start();
    else if (!glimmt && fadeTimer_->isActive()) fadeTimer_->stop();
}

void LiveValuesWidget::rebuildHtml() {
    if (rows_.empty()) {
        label_->setText(tr("RAM: -"));
        fadeTimer_->stop();
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Dreispaltige Tabelle statt einer Zeile pro Adresse. letter-spacing
    // bewusst nicht gesetzt: in MEGA-RAW stand dort ein durchgehender
    // Byte-Strom, wo die Sperrung sinnvoll war -- bei einzelnen "$ADDR WERT"
    // Paaren zerfaserte sie nur die Spaltenkanten.
    QString html = "<div style=\"font-family:'Consolas','Courier New',monospace; "
                   "font-size:11pt; line-height:165%;\">"
                   "<table cellspacing=\"0\" cellpadding=\"0\" width=\"100%\">";

    int spalte = 0;
    for (const auto& [slot, row] : rows_) {
        if (spalte == 0) html += "<tr>";

        QString addrHex = QString("$%1").arg(row.addr, 4, 16, QChar('0')).toUpper();
        QString hexStr = (row.value < 0)
            ? "--" : QString("%1").arg(row.value, 2, 16, QChar('0')).toUpper();
        QString col = colorForAge(row.lastChangeMs, now);

        html += QString("<td width=\"33%\">"
                        "<span style=\"color:#5a7a6a;\">%1</span>&nbsp;"
                        "<span style=\"color:%2; font-weight:bold;\">%3</span>"
                        "</td>")
                    .arg(addrHex).arg(col).arg(hexStr);

        if (++spalte == kSpalten) { html += "</tr>"; spalte = 0; }
    }
    // Angefangene Zeile mit leeren Zellen auffuellen, sonst dehnt Qt die
    // letzte gefuellte Zelle ueber die volle Breite und die Spalten
    // verspringen in der Schlusszeile.
    if (spalte != 0) {
        while (spalte++ < kSpalten) html += "<td width=\"33%\"></td>";
        html += "</tr>";
    }
    html += "</table></div>";
    label_->setText(html);

    updateFadeTimer();
}
