#include <windows.h>

// Minimal hollow host. Console subsystem so GetStdHandle works for any payload.
// Spawned suspended by RunPE and hollowed before this code ever runs.
int main() {
    while (TRUE) Sleep(10000);
    return 0;
}
