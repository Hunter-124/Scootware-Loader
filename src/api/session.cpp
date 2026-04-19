#include "session.h"
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <vector>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

namespace Session {

    fs::path GetSessionFilePath() {
        PWSTR path = NULL;
        if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path) == S_OK) {
            fs::path appData(path);
            CoTaskMemFree(path);
            fs::path scootwareDir = appData / "Scootware";
            if (!fs::exists(scootwareDir)) {
                fs::create_directories(scootwareDir);
            }
            return scootwareDir / "session.dat";
        }
        return "";
    }

    bool SaveCredentials(const std::string& username, const std::string& password) {
        std::string data = username + ":" + password;
        
        DATA_BLOB input;
        input.pbData = (BYTE*)data.c_str();
        input.cbData = (DWORD)data.length();

        DATA_BLOB output;
        if (CryptProtectData(&input, L"ScootwareSession", NULL, NULL, NULL, 0, &output)) {
            fs::path filePath = GetSessionFilePath();
            if (filePath.empty()) return false;

            std::ofstream file(filePath, std::ios::binary);
            if (file.is_open()) {
                file.write((char*)output.pbData, output.cbData);
                file.close();
                LocalFree(output.pbData);
                return true;
            }
            LocalFree(output.pbData);
        }
        return false;
    }

    bool LoadCredentials(Credentials& out) {
        fs::path filePath = GetSessionFilePath();
        if (filePath.empty() || !fs::exists(filePath)) return false;

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
        if (file.read(buffer.data(), size)) {
            DATA_BLOB input;
            input.pbData = (BYTE*)buffer.data();
            input.cbData = (DWORD)size;

            DATA_BLOB output;
            if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output)) {
                std::string decrypted((char*)output.pbData, output.cbData);
                LocalFree(output.pbData);

                size_t colonPos = decrypted.find(':');
                if (colonPos != std::string::npos) {
                    out.username = decrypted.substr(0, colonPos);
                    out.password = decrypted.substr(colonPos + 1);
                    return true;
                }
            }
        }
        return false;
    }

    void ClearCredentials() {
        fs::path filePath = GetSessionFilePath();
        if (!filePath.empty() && fs::exists(filePath)) {
            fs::remove(filePath);
        }
    }

    bool HasSavedCredentials() {
        fs::path filePath = GetSessionFilePath();
        return !filePath.empty() && fs::exists(filePath);
    }
}
