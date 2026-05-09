#pragma once
#include "mesh.h"
#include "material.h"
#include <string>
#include <memory>
#include "vector2d.h"

/// MeshImporter: Statische Klasse zum Laden von OBJ-Dateien mit Materialunterstützung
class MeshImporter {
public:
    /// Importiert eine OBJ-Datei inklusive Normals und MTL-Materialien
    /// \param filename Pfad zur OBJ-Datei
    /// \param defaultMat Material, falls kein MTL oder kein usemtl vorhanden
    /// \return Ein unique_ptr<Mesh> oder nullptr bei Fehler
    static std::unique_ptr<Mesh> importOBJ(const std::string& filename, const Material& defaultMat);
    static std::unique_ptr<Mesh> importOBJ_fast(const std::string &filename, const Material &defaultMat);
};

void skipSpaces(const char *&p);

double parsedouble(const char *&p);

int parseInt(const char *&p);

void trimRange(const char *&start, const char *&end);

int fixIndex(int idx, int size);
