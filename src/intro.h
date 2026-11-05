// Intro -- Choreografie aus rawnes_intro.html, wie in intro_window.cpp der
// Qt-Fassung. Alle Zeiten, Groessen, Farben und Bildmasse (FMETA) stammen von
// dort. Gezeichnet wird mit der Zeichenliste von Dear ImGui statt mit
// QPainter; was QPainter direkt konnte (radiale Verlaeufe, Weichzeichnen des
// Textscheins), ist hier angenaehert.
#pragma once

#include "imgui.h"

namespace intro {

// Einmal nach dem Anlegen des OpenGL-Kontexts: Katzenbilder hochladen.
// fett/kursiv: Courier New in fett und kursiv (nullptr = Standardschrift).
void Laden(ImFont* fett, ImFont* kursiv);

// Startet die Sequenz (600 ms Vorlauf, dann 6200 ms).
void Starten();

// Zeichnet ein Bild ueber das ganze Fenster. Liefert false, sobald die
// Sequenz durch ist oder uebersprungen wurde (Klick, Taste, SKIP).
bool Zeichnen(float skalierung);

void Freigeben();

}  // namespace intro
