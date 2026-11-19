#pragma once
#include <string>
#include <vector>

class object;

// Ganze Szene (alle Objekte, ohne Lichter und Kamera) als eine OBJ-Datei mit MTL daneben.
// Jedes Objekt wird eine eigene Gruppe "o <Name>", jede Farbe ein Material.
// Kugeln und Zylinder werden dafuer in Dreiecke zerlegt.
// Implementiert in SceneSaver.cpp. meldung: Zusammenfassung bzw. Fehlertext.
bool szeneAlsOBJExportieren(const std::string &datei, std::string &meldung);

// Nur diese Objekte exportieren (Auswahl); nullptr = ganze Szene. Vom Aufrufer kurz gesetzt.
inline const std::vector<object *> *objExportNur = nullptr;
