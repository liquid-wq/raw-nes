#pragma once
#include <QWidget>

class QCheckBox;
class QLabel;

// Hardcore-Checkbox + Hinweistext, entspricht hardcore_cb/hardcore_hint
// in der Python-GUI. Die eigentliche Pruefung (ct_ss_on/ct_gg_on) liegt
// NICHT hier -- dieses Widget ist nur die Anzeige/der Schalter, die
// Logik lebt im Poll-Loop (monitor_worker-Aequivalent, noch zu bauen).
class HardcorePanel : public QWidget {
    Q_OBJECT
public:
    explicit HardcorePanel(QWidget* parent = nullptr);

    bool isChecked() const;
    void setChecked(bool checked); // programmatisch setzen (z.B. Downgrade bei Verstoss),
                                   // loest toggled() aus wie ein Nutzerklick

    void setHint(const QString& text);

signals:
    void toggled(bool checked);

private:
    QCheckBox* checkbox_;
    QLabel* hint_;
};
