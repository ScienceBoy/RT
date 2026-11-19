#pragma once
// Material-Pinsel: Material und Textur eines Objekts aufnehmen (Rechtsklick -> "Material aufnehmen") und per Klick
// in einer Ansicht auf andere Objekte oder Teile uebertragen. Die Ansichten (wireframe.cpp) melden den Klick hier;
// das Uebertragen selbst steht in toolwindow.cpp. Nur als Header.
#include <functional>

class object;

namespace materialPinsel
{
inline bool gAktiv = false;
inline std::function<void(object *)> gAnwenden; // Klick in einer Ansicht: Material auf dieses Objekt bzw. Teil
} // namespace materialPinsel
