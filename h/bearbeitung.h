#pragma once
// Bearbeiten: Mehrfachauswahl, Rueckgaengig/Wiederholen, Kopieren, Speicherstand.
//
// Rueckgaengig merkt sich bei Aenderungen den Zustand der betroffenen Objekte vorher und
// nachher (Geometrie, Farbe, Name, Drehung). Geloeschte Objekte werden nicht zerstoert, sondern
// im Verlauf aufbewahrt, damit sie zurueckkommen koennen.
//
// Nach dem Leeren oder Laden der Szene muss verlaufLeeren() aufgerufen werden: dort werden
// Objekte zerstoert, auf die der Verlauf noch zeigt.

#include "object.h"
#include "light.h"
#include <atomic>
#include <memory>
#include <utility>
#include <string>
#include <vector>

// ---------- Mehrfachauswahl ----------
// selectedObject ist das Hauptobjekt (Eigenschaften, Liste); weitere Objekte kommen mit Strg+Klick dazu.
// Aendert anderer Code selectedObject direkt, gilt wieder nur dieses eine Objekt.

std::vector<object *> ausgewaehlteObjekte();                 // Hauptobjekt zuerst; leer ohne Objektauswahl
void auswahlSetzen(const std::vector<object *> &objekte); // erstes = Hauptobjekt; leer = keine Auswahl
void auswahlUmschalten(object *o);                          // Strg+Klick: dazu bzw. wieder weg
bool istAusgewaehlt(const object *o);

// Bounding-Box ueber alle Objekte (false bei leerer Liste)
bool auswahlGrenzen(const std::vector<object *> &objekte, Vector3D &minimum, Vector3D &maximum);

// ---------- Rueckgaengig / Wiederholen ----------

// Zustand vor einer Aenderung merken; aenderungAbschliessen() nimmt den Zustand danach und legt
// den Schritt in den Verlauf. Beides auf dem Hauptthread.
void aenderungBeginnen(const std::string &name, const std::vector<object *> &objekte,
                       const std::vector<light *> &lichter = {});
void aenderungAbschliessen();

// Objekte/Lichter wurden gerade in die Szene eingefuegt
void eingefuegtMerken(const std::string &name, const std::vector<object *> &objekte,
                      const std::vector<light *> &lichter = {});

// Objekte/Lichter aus der Szene nehmen (bleiben fuer Rueckgaengig erhalten); hebt die Auswahl auf
void loeschenMitVerlauf(const std::string &name, const std::vector<object *> &objekte,
                        const std::vector<light *> &lichter = {});

bool rueckgaengig();
bool wiederholen();
std::string naechstesRueckgaengig(); // Name des naechsten Schritts, leer wenn keiner
std::string naechstesWiederholen();
void verlaufLeeren();

// ---------- Gruppen ----------
class Gruppe;
// ausgewaehlte Objekte (direkt in der Szene) zu einer Gruppe; liefert sie (ausgewaehlt) oder nullptr
Gruppe *gruppieren(const std::vector<object *> &objekte, const std::string &name);
// Objekte (aus der Szene oder anderen Gruppen) an das Ende einer bestehenden Gruppe haengen - ein Schritt im Verlauf
bool zurGruppeHinzufuegen(Gruppe *ziel, const std::vector<object *> &objekte);
// Teile von Gruppen wieder direkt in die Szene legen - ein Schritt im Verlauf
bool ausGruppeNehmen(const std::vector<object *> &objekte);
// Gruppe in einzelne Objekte zerlegen (ausgewaehlt); false, wenn sie nicht direkt in der Szene liegt
bool gruppeAufheben(Gruppe *gruppe);
// Objekt durch ein neues an derselben Stelle ersetzen (z.B. Baum -> Gruppe aus Stamm und Blaettern)
void ersetzenMitVerlauf(const std::string &name, object *alt, std::unique_ptr<object> neu);
// wie ersetzenMitVerlauf, dazu werden 'entfernen' (direkt in der Szene) geloescht - ein Schritt im Verlauf
// (Objekte addieren / subtrahieren)
void ersetzenUndEntfernenMitVerlauf(const std::string &name, object *alt, std::unique_ptr<object> neu,
                                    const std::vector<object *> &entfernen);
// Baum -> neue Gruppe aus zwei Meshes "Stamm" und "Blätter" (nullptr, wenn kein Baum)
std::unique_ptr<object> baumZerlegen(const object *baum);
// Gruppe, die das Objekt enthaelt (nullptr: liegt direkt in der Szene)
Gruppe *gruppeVon(const object *o);
// Grenzen aller Gruppen neu berechnen (nach Aenderungen an Objekten darin)
void alleGruppenAktualisieren();

// ---------- Speicherstand ----------
bool szeneGeaendert();
void szeneGeaendertMarkieren(); // auch aus dem Import-Thread

// Dreieckslisten eines Objekts zum Bearbeiten (Mesh; Baum: Holz und Blaetter; 3D-Text; Pfeil); leer sonst
class Dreieck;
std::vector<std::vector<Dreieck> *> objektDreiecke(object *o);
void szeneGespeichertMarkieren();
const std::string &aktuelleDatei(); // zuletzt geladene/gespeicherte XML, leer wenn keine
void aktuelleDateiSetzen(const std::string &datei);

// ---------- Kopieren ----------
// Kopie fuer Duplizieren/Einfuegen; nullptr, wenn der Typ nicht kopiert werden kann.
// Meshes und Baeume werden als Mesh kopiert.
std::unique_ptr<object> objektKopieren(const object *o); // Instanz: weitere Instanz derselben Vorlage

// Instanzen (verknuepfte Kopien, instanz.h)
// mehrere Objekte je durch ein neues ersetzen (an derselben Stelle, auch in Gruppen) - ein Schritt im Verlauf;
// liefert die neuen Objekte (sind danach ausgewaehlt)
std::vector<object *> ersetzenMehrereMitVerlauf(const std::string &name,
                                                std::vector<std::pair<object *, std::unique_ptr<object>>> paare);
std::unique_ptr<object> alsInstanz(const object *o);       // Objekt -> Instanz (Kopie wird Vorlage), gleiche Lage
std::unique_ptr<object> instanzAufloesen(const object *o); // Instanz -> eigenstaendige Kopie in ihrer Lage
bool nurInstanzen(const object *o);  // Instanz oder Gruppe nur aus Instanzen
bool hatInstanzen(const object *o);  // enthaelt (auch in Gruppen) eine Instanz
size_t instanzenVon(const object *vorlage);                // so viele Instanzen benutzen die Vorlage
std::unique_ptr<light> lichtKopieren(const light *l);

// Autom. Rendern: jede Aenderung meldet ihren Bereich (Weltquader) bzw. "ganzes Bild" (Licht, Kamera, Einstellungen);
// gAutoRenderStand zaehlt jede Benutzeraktivitaet (Wartezeit beginnt neu). Abholen leert die Sammlung.
extern std::atomic<unsigned> gAutoRenderStand;
void autoRenderAktivitaet();
void autoRenderBereich(const Vector3D &mn, const Vector3D &mx);
void autoRenderGanz();
extern std::atomic<bool> gAutoAuswahlGeaendert; // szeneGeaendertMarkieren: Bereich = Auswahl
bool autoRenderAbholen(std::vector<std::pair<Vector3D, Vector3D>> &bereiche, bool &ganz);
