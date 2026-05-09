#include "obj_import_dialog.h"
#include "farbe.h"
#include "material.h"
#include <commdlg.h>
#include <cstdlib>
#include <future>
#include <iostream>
#include "scene.h"
#include "SceneLoader.h"

std::mutex sceneMutexImport;
OBJImportDialog::OBJImportDialog(std::vector<std::unique_ptr<object>>& sceneRef)
    : scene(sceneRef)
{}

std::string OBJImportDialog::openFileDialog() {
    OPENFILENAMEW ofn;  // <-- W für Unicode
    WCHAR szFile[MAX_PATH] = L"";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;  // optional Fensterhandle
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"OBJ Dateien\0*.obj\0Alle Dateien\0*.*\0"; // Unicode
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {  // <-- W-Version
        // WCHAR -> UTF-8
        int len = WideCharToMultiByte(CP_UTF8, 0, ofn.lpstrFile, -1, nullptr, 0, nullptr, nullptr);
        std::string str(len - 1, 0); // -1 um Nullterminator zu ignorieren
        WideCharToMultiByte(CP_UTF8, 0, ofn.lpstrFile, -1, &str[0], len, nullptr, nullptr);
        return str;
    }

    return "";
}

void OBJImportDialog::openAndImport() {
    std::string filename = openFileDialog();
    if(filename.empty()) {
        std::cout << "Import abgebrochen.\n";
        return;
    }

    // Beispiel: Default-Material Rot
    Material mat = makeMaterialSimple(Farbe(1,0,0));

    auto future = std::async(std::launch::async, [this, filename, mat]() {
        auto mesh = MeshImporter::importOBJ_fast(filename, mat);

        if (mesh) {
            {
                std::lock_guard<std::mutex> lock(sceneMutexImport);
                scene.push_back(std::move(mesh));
                computeSceneBounds();
            }

            std::cout << "Mesh geladen: " << filename << "\n";
        }
    });
}

void OBJImportDialog::import(const std::string& filename)
{
    Material mat = makeMaterialSimple(Farbe(1,0,0));

    auto mesh = MeshImporter::importOBJ_fast(filename, mat);

    if (mesh) {
        scene.push_back(std::move(mesh));
        computeSceneBounds();
    }
}