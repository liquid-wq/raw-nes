// Bilder: PNG dekodieren (GDI+, gehoert zu Windows), als OpenGL-Textur
// hochladen, eingebettete Ressourcen lesen.
//
// OpenGL 1.1 (die Software-Fassung von Windows) kennt nur Texturen mit
// Zweierpotenz-Kanten. Bilder werden deshalb in die naechstgroessere
// Zweierpotenz gelegt; u/v geben den belegten Ausschnitt an.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bilder {

struct Textur {
    unsigned id = 0;
    int w = 0, h = 0;      // Bildgroesse in Pixeln
    float u = 1.0f, v = 1.0f;  // belegter Ausschnitt der Textur
    bool ok() const { return id != 0; }
};

bool Start();   // GDI+ hochfahren, einmal beim Programmstart
void Ende();

bool PngDekodieren(const void* daten, size_t n, int* w, int* h, std::vector<uint8_t>* rgba);

// pixelig: Nachbarpixel statt Glaettung (Pixel-Art bleibt knackig).
Textur Hochladen(const uint8_t* rgba, int w, int h, bool pixelig);
Textur PngHochladen(const void* daten, size_t n, bool pixelig);
void Freigeben(Textur& t);

// Eingebettete Windows-Ressource (RCDATA) mit dieser Kennung.
bool Ressource(int id, const void** daten, size_t* n);

}  // namespace bilder
