#pragma once
// Einstellungen einer Textur je Objekt (Material-Reiter), zusaetzlich zu Bild, Abbildungsart, Spiegeln und
// 90-Grad-Drehung (die stehen weiter direkt im object):
//  - kachel:      Groesse einer Bildkachel in Metern (0 = Bild einmal ueber das ganze Objekt wie bisher)
//  - versatzU/V:  Verschiebung des Bilds in Bildbreiten (0,5 = halbes Bild)
//  - winkel:      freie Drehung des Bilds in Grad (zusaetzlich zu den 90-Grad-Schritten)
//  - kugelDrehung: Drehung der Kugel-Abbildung (Grad um X, Y, Z) - vorher eine globale Einstellung fuer alle Kugeln
//  - objektfest:  Textur haftet am Objekt (dreht und bewegt sich mit, auch in Animationen); aus = Projektion in
//                 Weltkoordinaten wie bisher
//  - weitere Bilder: Relief (Hoehenbild, Graustufen), Alpha (Ausschneiden), Glanz (hell = glaenzend), Leuchten
// Das Objekt haelt die Einstellungen als shared_ptr<const ...>: Dreiecke eines Netzes teilen sie, Rueckgaengig-
// Zustaende kopieren nur den Zeiger. Aendern = neue Kopie anlegen (texturParAendern in textur_abbildung.h).

#include "vector3d.h"
#include <memory>
#include <string>

class Texture;

enum TexturBildArt
{
    BILD_RELIEF = 0,  // Hoehenbild: hell = erhaben; Staerke = Tiefe in Metern
    BILD_ALPHA = 1,   // Ausschneiden: dunkel (< 50 %) = Loch; Staerke ohne Bedeutung
    BILD_GLANZ = 2,   // hell = glaenzend/spiegelnd, dunkel = matt; Staerke = Anteil (0..1)
    BILD_LEUCHTEN = 3, // Leuchtfarbe; Staerke = Helligkeit
    BILD_UNTEN = 4,    // Unterseite (z.B. weisser Bauch): unterhalb der Grenze diese Farbe; Staerke = Hoehe der Grenze 0..1
    BILD_ANZAHL = 5
};

struct TexturParameter
{
    double kachel = 0.0;
    double versatzU = 0.0, versatzV = 0.0;
    double winkel = 0.0;
    Vector3D kugelDrehung = Vector3D(0, 0, 0);
    bool objektfest = false; // aeltere Szenen: wie bisher; neue Texturen bekommen "an"
    bool fellHuelle = false; // Huelle der Fell-Imitation: wirft kaum Schatten (sonst wird das Fell darunter fast schwarz)
    double regenRinge = 0.0; // Wellenringe der Regentropfen (Wasser), 0 = aus, 1 = normal (regen.h; nur bei Regen)
    double nass = 0.0;       // nasse Oberflaeche 0..1: dunkler und glaenzender (auch ohne Regen)

    std::shared_ptr<Texture> bild[BILD_ANZAHL];
    std::string bildName[BILD_ANZAHL];
    double staerke[BILD_ANZAHL] = {0.01, 1.0, 1.0, 1.0, 0.35};

    bool hatBild(int art) const { return art >= 0 && art < BILD_ANZAHL && bild[art] != nullptr; }
    bool hatWeitereBilder() const
    {
        for (int i = 0; i < BILD_ANZAHL; ++i)
            if (bild[i])
                return true;
        return false; // (Regenringe/Nass sind keine Bilder)
    }

    static const TexturParameter &vorgabe()
    {
        static const TexturParameter p;
        return p;
    }
};
