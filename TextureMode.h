#pragma once
// Wie eine Textur aufs Objekt gelegt wird (BOX = Wuerfel: jede Seite aus ihrer eigenen Richtung)
// TRIPLANAR = Wuerfel weich: drei Seiten nach der Flaechennormale gemischt (keine harten Naehte an Steinen/Terrain)
enum class TextureMode { None, SPHERE, PLANAR, UV, CYLINDER, BOX, TRIPLANAR };
