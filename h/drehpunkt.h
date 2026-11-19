#pragma once
// Eigener Drehpunkt fuer Drehen und Skalieren (Modus Rotate/Scale): in den Ansichten als orangefarbene
// Markierung, mit der Maus verschiebbar (Shift+Klick: zurueck zur Mitte der Auswahl). Ohne Verschieben
// liegt er in der Mitte der Auswahl; bei einer anderen Auswahl springt er wieder dorthin.

#include "bearbeitung.h"
#include "vector3d.h"
#include "wireframe.h"
#include <vector>

inline bool drehpunktEigen = false;           // vom Benutzer verschoben
inline Vector3D drehpunktPos;                 // dann diese Stelle
inline std::vector<object *> drehpunktAuswahl; // fuer diese Auswahl
inline bool drehpunktZiehen = false;          // wird gerade mit der Maus gezogen

// sichtbar und wirksam nur im Modus Drehen/Skalieren mit ausgewaehlten Objekten
inline bool drehpunktSichtbar()
{
    return selectedType == SelectedType::OBJECT &&
           (selectedAction == SelectedAction::ROTATE || selectedAction == SelectedAction::SCALE) &&
           !ausgewaehlteObjekte().empty();
}

inline Vector3D drehpunkt()
{
    std::vector<object *> sel = ausgewaehlteObjekte();
    if (drehpunktEigen && sel == drehpunktAuswahl)
        return drehpunktPos;
    Vector3D mn, mx;
    if (auswahlGrenzen(sel, mn, mx))
        return (mn + mx) * 0.5;
    return drehpunktPos;
}

inline void drehpunktSetzen(const Vector3D &p)
{
    drehpunktEigen = true;
    drehpunktPos = p;
    drehpunktAuswahl = ausgewaehlteObjekte();
}

inline void drehpunktZuruecksetzen() { drehpunktEigen = false; }

// Hauptthread (Abgleich): andere Auswahl -> Drehpunkt wieder in der Mitte
inline void drehpunktAbgleichen()
{
    if (drehpunktEigen && !drehpunktZiehen && ausgewaehlteObjekte() != drehpunktAuswahl)
        drehpunktEigen = false;
}
