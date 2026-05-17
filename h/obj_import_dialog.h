#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include "mesh.h"
#include "mesh_importer.h"
#include "material.h"

/// OBJImportDialog: Öffnet einen Windows-Datei-Dialog und importiert ein OBJ-Mesh
class OBJImportDialog {
public:
    /// Konstruktor
    /// \param sceneRef Referenz auf die Szene (vector<unique_ptr<object>>)
    explicit OBJImportDialog(std::vector<std::unique_ptr<object>>& sceneRef);

    /// Öffnet den Dialog, lädt das Mesh und fügt es der Szene hinzu
    void openAndImport();
    void import(const std::string &filename);
    std::string openFileDialog();

private:
    std::vector<std::unique_ptr<object>>& scene;
};