#pragma once
#include <QWidget>

class QPlainTextEdit;

// Log-Fenster, entspricht log_text in der Python-GUI. Haengt bewusst
// keine Zeitstempel an -- das tat die Python-Version auch nicht:
// Playtime-Tracking laeuft ueber RA-Server-Pings, nicht ueber lokale
// Zeitstempel-Protokollierung.
class LogPanel : public QWidget {
public:
    explicit LogPanel(QWidget* parent = nullptr);

    void append(const QString& text); // Zeile anhaengen, scrollt automatisch ans Ende
    void clear();

private:
    QPlainTextEdit* text_;
};
