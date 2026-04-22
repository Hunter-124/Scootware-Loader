#pragma once
//
// handoff.h
//
// Loader -> External secrets handoff. There are two parallel channels;
// the external tries them in order so a failure of either is non-fatal.
//
//   1. Inherited environment block (legacy, low-friction).
//      The loader sets SW_S/SW_P/SW_E in its OWN env block AND passes
//      a freshly-built env block explicitly to CreateProcess via
//      lpEnvironment. The child reads them with GetEnvironmentVariableA
//      and wipes them on first read.
//
//      Why both? Process hollowing performs CreateRemoteThread calls into
//      the child for every imported DLL, which forces an early
//      LdrInitializeThunk run. On certain Windows builds this re-walks
//      the PEB->ProcessParameters region and intermittently drops env
//      vars set on the parent right before CreateProcess. Passing the
//      env block explicitly bypasses that race.
//
//   2. Named file-mapping fallback (`Local\SW_HANDOFF_<loader_pid>`).
//      A 4 KiB shared-memory section that holds the same wrapped blob.
//      The child looks up its parent PID and opens the mapping by name.
//      This survives any future CreateProcess / RunPE quirk that strips
//      env vars: as long as the loader process is still alive (we hold
//      the section open until well after the cheat has read it), the
//      handoff is reachable.
//
// The token wrap (XOR with HMAC(HWID, "SW_S_KEY_v1")) is identical in
// both channels. The HWID is local-only, so even if the shared-memory
// section is sniffed by a rogue process in the same session, the token
// is still useless without the victim's HWID.
//
// CONTRACT (must stay in lockstep with Scootware-External/core/auth/session.cpp)
//   SW_S = base64( token XOR hmac_sha256(hwid, "SW_S_KEY_v1")[:len(token)] )
//   SW_P = productId  (plain — not sensitive on its own)
//   SW_E = expiresAt  (plain ISO-8601 — already public to the user)
//
// USAGE
//   Handoff::Set(token, hwid, productId, expiresAt);
//   auto envBlock = Handoff::BuildChildEnvironment();
//   RunPE::Execute(payload, allocSize, hostPath, envBlock);
//   Handoff::Wipe();   // wipes env vars + closes the shmem section
//

#include <string>
#include <vector>

namespace Handoff {

    // Wraps the token with HWID-keyed XOR + base64, stashes SW_S/SW_P/SW_E
    // into the current process's environment block, AND publishes the same
    // wrapped blob in a Local\SW_HANDOFF_<pid> file mapping.
    // Returns true on success.
    bool Set(const std::string& token,
             const std::string& hwid,
             const std::string& productId,
             const std::string& expiresAt);

    // Returns a fresh environment block (KEY=VAL\0 ... \0\0) suitable for
    // passing as the lpEnvironment argument of CreateProcessA. The block
    // is the loader's current env at the time of the call (so SW_S/SW_P/SW_E
    // are included if Set() ran). Empty vector on failure.
    //
    // Calling CreateProcessA with this block instead of NULL avoids any
    // ambiguity around env-block inheritance during process hollowing.
    std::vector<char> BuildChildEnvironment();

    // Removes SW_S/SW_P/SW_E from the loader's env block and closes the
    // shared-memory section. Safe to call repeatedly. Should be called
    // once the cheat has had a chance to read the handoff (in practice:
    // a few seconds after RunPE — long enough for session::bootstrap()
    // to complete on the child side).
    void Wipe();
}
