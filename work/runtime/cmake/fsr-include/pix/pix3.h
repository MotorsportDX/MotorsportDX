// Stands in for the PIX header of the FidelityFX SDK's libs folder, which setup does not fetch
// (cmake/rex_fsr.cmake). The SDK's DX12 backend includes it but loads the two PIX functions it
// uses, PIXBeginEventOnCommandList and PIXEndEventOnCommandList, from WinPixEventRuntime.dll
// at run time (ffxLoadPixDll), so it needs no declaration from it.
#pragma once
