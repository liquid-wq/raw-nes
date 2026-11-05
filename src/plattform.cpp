#include "plattform.h"

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

namespace plattform {

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring Breit(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string ExeOrdner() {
    std::wstring pfad(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, &pfad[0], (DWORD)pfad.size());
        if (n < pfad.size()) { pfad.resize(n); break; }
        pfad.resize(pfad.size() * 2);
    }
    const size_t trenner = pfad.find_last_of(L"\\/");
    if (trenner != std::wstring::npos) pfad.resize(trenner);
    // Wie applicationDirPath(): Schraegstriche nach vorn.
    for (wchar_t& c : pfad) if (c == L'\\') c = L'/';
    return Utf8(pfad);
}

namespace {

// Gemeinsamer Teil von Ordner- und Dateiauswahl ueber IFileOpenDialog --
// derselbe Windows-Dialog, den Qt unter Windows zeigt.
std::string Dialog(void* fenster, const std::string& titel, bool ordner, const std::string& start,
                   const std::vector<std::pair<std::string, std::string>>* filter) {
    std::string ergebnis;
    IFileOpenDialog* d = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(&d))))
        return ergebnis;

    DWORD optionen = 0;
    d->GetOptions(&optionen);
    optionen |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
    if (ordner) optionen |= FOS_PICKFOLDERS;
    else optionen |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
    d->SetOptions(optionen);
    d->SetTitle(Breit(titel).c_str());

    std::vector<std::wstring> texte;
    std::vector<COMDLG_FILTERSPEC> spec;
    if (filter && !filter->empty()) {
        texte.reserve(filter->size() * 2);
        for (const auto& f : *filter) {
            texte.push_back(Breit(f.first));
            texte.push_back(Breit(f.second));
        }
        for (size_t i = 0; i < filter->size(); ++i)
            spec.push_back({texte[i * 2].c_str(), texte[i * 2 + 1].c_str()});
        d->SetFileTypes((UINT)spec.size(), spec.data());
    }

    if (!start.empty()) {
        IShellItem* ort = nullptr;
        if (SUCCEEDED(::SHCreateItemFromParsingName(Breit(start).c_str(), nullptr, IID_IShellItem,
                                                    reinterpret_cast<void**>(&ort)))) {
            d->SetFolder(ort);
            ort->Release();
        }
    }

    if (SUCCEEDED(d->Show(static_cast<HWND>(fenster)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(d->GetResult(&item))) {
            PWSTR pfad = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pfad))) {
                ergebnis = Utf8(pfad);
                ::CoTaskMemFree(pfad);
            }
            item->Release();
        }
    }
    d->Release();
    // Wie Qt: Schraegstriche nach vorn.
    for (char& c : ergebnis) if (c == '\\') c = '/';
    return ergebnis;
}

}  // namespace

std::string OrdnerWaehlen(void* fenster, const std::string& titel, const std::string& start) {
    return Dialog(fenster, titel, true, start, nullptr);
}

std::string DateiWaehlen(void* fenster, const std::string& titel,
                         const std::vector<std::pair<std::string, std::string>>& filter) {
    return Dialog(fenster, titel, false, std::string(), &filter);
}

void UrlOeffnen(const std::string& url) {
    ::ShellExecuteW(nullptr, L"open", Breit(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace plattform
