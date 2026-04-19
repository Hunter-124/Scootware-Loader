#pragma once
#include <string>

namespace Session {
    struct Credentials {
        std::string username;
        std::string password;
    };

    // Encrypts and saves credentials to %appdata%/Scootware/session.dat
    bool SaveCredentials(const std::string& username, const std::string& password);

    // Loads and decrypts credentials from %appdata%/Scootware/session.dat
    bool LoadCredentials(Credentials& out);

    // Deletes the session data file
    void ClearCredentials();

    // Checks if the session data file exists
    bool HasSavedCredentials();
}
