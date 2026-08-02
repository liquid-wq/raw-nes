#pragma once
#include <QWidget>
#include <QColor>
#include <vector>

// Sieben-Segment-LED-Anzeige im "Time Circuits"-Armaturenbrett-Stil
// (Zurueck in die Zukunft): dunkles Panel, Segmente mit Verlaufs-
// Hochglanz + mehrschichtigem Glow, ungenutzte Segmente scheinen als
// "Geister"-Segment schwach durch. Ein SevenSegmentDigit stellt genau
// EIN Zeichen dar (0-9, A-F, '-', Leerzeichen). LedDigitStrip reiht
// mehrere davon horizontal, dehnt sich mit dem verfuegbaren Platz
// (QSizePolicy::Expanding) statt auf Mindestgroesse zu verharren.

class SevenSegmentDigit : public QWidget {
    Q_OBJECT
public:
    explicit SevenSegmentDigit(QWidget* parent = nullptr);

    void setChar(QChar c);
    void setColor(const QColor& color);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QChar ch_ = ' ';
    QColor color_;
};

class LedDigitStrip : public QWidget {
    Q_OBJECT
public:
    // digitCount: feste Anzahl Stellen (rechtsbuendig, mit Leerzeichen
    // aufgefuellt bzw. von links abgeschnitten wenn der Text laenger ist).
    explicit LedDigitStrip(int digitCount, QWidget* parent = nullptr);

    void setText(const QString& text);
    void setColor(const QColor& color);

private:
    std::vector<SevenSegmentDigit*> digits_;
};
