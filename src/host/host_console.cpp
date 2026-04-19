#include <windows.h>

// Minimal console hollow host. Spawned suspended and hollowed by RunPE for
// CONSOLE-subsystem payloads. Keeps stdout/stdin handles valid natively.
int main() {
    while (TRUE) Sleep(10000);
    return 0;
}
