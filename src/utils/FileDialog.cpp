#include "FileDialog.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#endif

namespace pt {
    // guard pa evitar re-entrada: Windows re-envia clicks al cerrar el dialogo
    // y eso puede re-disparar el boton que abrio el dialogo → bucle infinito
    static bool s_dialogOpen = false;

    std::optional<std::filesystem::path> openImageFileDialog() {
    #ifdef _WIN32
        if (s_dialogOpen) return std::nullopt;
        s_dialogOpen = true;

        OPENFILENAMEW ofn{};
        wchar_t fileBuffer[MAX_PATH] = {0};
        wchar_t filter[] = L"Image Files (*.webp;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tiff;*.tif)\0*.webp;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tiff;*.tif\0All Files (*.*)\0*.*\0\0";

        // pillar la ventana de GD pa que el dialogo sea modal de verdad
        // asi windows bloquea el input al juego mientras esta abierto
        HWND gdWindow = GetForegroundWindow();

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = gdWindow;
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.lpstrFile = fileBuffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
        ofn.lpstrDefExt = L"png";

        std::optional<std::filesystem::path> result = std::nullopt;
        if (GetOpenFileNameW(&ofn)) {
            result = std::filesystem::path(fileBuffer);
        }
        // demorar el reset al siguiente frame pa que los clicks fantasma
        // que Windows re-envia al cerrar el dialogo se descarten
        Loader::get()->queueInMainThread([] { s_dialogOpen = false; });
        return result;
    #else
        // macOS/iOS/Android: Not implemented yet.
        log::warn("[FileDialog] Image file dialog not supported on this platform");
        return std::nullopt;
    #endif
    }

    std::optional<std::filesystem::path> saveImageFileDialog(const std::wstring& defaultName) {
    #ifdef _WIN32
        if (s_dialogOpen) return std::nullopt;
        s_dialogOpen = true;

        OPENFILENAMEW ofn{};
        wchar_t fileBuffer[MAX_PATH] = {0};
        // rellenar nombre por defecto
        wcsncpy_s(fileBuffer, defaultName.c_str(), _TRUNCATE);
        wchar_t filter[] = L"PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0\0";

        HWND gdWindow = GetForegroundWindow();

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = gdWindow;
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.lpstrFile = fileBuffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
        ofn.lpstrDefExt = L"png";

        std::optional<std::filesystem::path> result = std::nullopt;
        if (GetSaveFileNameW(&ofn)) {
            std::filesystem::path p(fileBuffer);
            // asegurar extension .png
            if (p.extension().wstring() != L".png") p.replace_extension(L".png");
            result = p;
        }
        Loader::get()->queueInMainThread([] { s_dialogOpen = false; });
        return result;
    #else
        // macOS/iOS/Android: Not implemented yet.
        log::warn("[FileDialog] Save file dialog not supported on this platform");
        return std::nullopt;
    #endif
    }
    
    std::optional<std::filesystem::path> openFolderDialog() {
    #ifdef _WIN32
        if (s_dialogOpen) return std::nullopt;
        s_dialogOpen = true;

        // IFileDialog (Windows Vista+) pa selector de carpeta
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        bool needsUninit = SUCCEEDED(hr);
        
        IFileDialog* pfd = nullptr;
        hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
        
        if (SUCCEEDED(hr)) {
            DWORD dwOptions;
            hr = pfd->GetOptions(&dwOptions);
            if (SUCCEEDED(hr)) {
                hr = pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
            }
            
            if (SUCCEEDED(hr)) {
                hr = pfd->Show(nullptr);
                if (SUCCEEDED(hr)) {
                    IShellItem* psi;
                    hr = pfd->GetResult(&psi);
                    if (SUCCEEDED(hr)) {
                        PWSTR pszPath = nullptr;
                        hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                        if (SUCCEEDED(hr)) {
                            std::filesystem::path result(pszPath);
                            CoTaskMemFree(pszPath);
                            psi->Release();
                            pfd->Release();
                            if (needsUninit) CoUninitialize();
                            Loader::get()->queueInMainThread([] { s_dialogOpen = false; });
                            return result;
                        }
                        psi->Release();
                    }
                }
            }
            pfd->Release();
        }
        
        if (needsUninit) CoUninitialize();
        Loader::get()->queueInMainThread([] { s_dialogOpen = false; });
        return std::nullopt;
    #else
        // macOS/iOS/Android: Not implemented yet.
        log::warn("[FileDialog] Folder dialog not supported on this platform");
        return std::nullopt;
    #endif
    }
}

