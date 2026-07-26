// D3D9 interception (plan.md 4.2.2).
#pragma once

#include <windows.h>

struct IDirect3D9;

// Installs the IDirect3D9::CreateDevice hook on the object returned by the real
// Direct3DCreate9. The device's Reset is hooked once the device exists.
void Hooks_InstallOnD3D9(IDirect3D9* d3d9);
