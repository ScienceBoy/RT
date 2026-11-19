#pragma once
// Fortschrittsanzeige fuer laenger dauernde Arbeiten (Speichern, Laden, OBJ-Import/-Export, Mengen-Operationen,
// Netz-Operationen, Absetzen ...). Dauert eine Arbeit laenger als ~0,3 s, erscheint ein kleines Fenster mit
// Balken und Text; der Taskleisten-Knopf zeigt den Fortschritt ebenfalls. Kurze Arbeiten zeigen nichts.
//
// Verwendung (nur im Thread der Oberflaeche wirksam, sonst ohne Wirkung):
//     fortschritt::Abschnitt a(L"Szene speichern");
//     for (size_t i = 0; i < n; ++i) { fortschritt::schritt(i, n, L"Objekt …"); ... }
// Abschnitte duerfen geschachtelt sein: ein innerer Abschnitt fuellt genau den Teil des Balkens, den der aeussere
// mit schritt(i, n) gerade bearbeitet (z.B. Speichern -> Objekt 3 von 10 -> OBJ-Datei schreiben 40 %).
// Definiert in toolwindow.cpp.

#include <cstddef>
#include <string>

namespace fortschritt
{
void beginnen(const std::wstring &titel);
void schritt(size_t i, size_t n, const std::wstring &text = std::wstring()); // Schritt i von n beginnt
void melden(double anteil, const std::wstring &text = std::wstring());      // 0..1 innerhalb des Abschnitts
// naechster Teil reicht von 'von' bis 'bis' (0..1 innerhalb des Abschnitts); ein innerer Abschnitt fuellt ihn
void bereich(double von, double bis, const std::wstring &text = std::wstring());
void ende();

struct Abschnitt
{
    explicit Abschnitt(const std::wstring &titel) { beginnen(titel); }
    ~Abschnitt() { ende(); }
    Abschnitt(const Abschnitt &) = delete;
    Abschnitt &operator=(const Abschnitt &) = delete;
};
} // namespace fortschritt
