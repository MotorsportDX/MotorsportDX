param(
    [ValidateSet('doctor','setup','generate','build','all','run','warmcache','profbuild','export','preflight','launcher-data','snapshot','launcher','package','mods')][string]$Action = 'doctor',
    [string]$Disc1,
    [string]$Disc2,
    [string[]]$Content,
    [string]$BuildRoot,
    [ValidateSet('mingw','msvc')][string]$Abi,
    [string]$MingwRoot,
    [string]$LLVMRoot,
    [string]$Toolchain,
    [string]$Python = 'python',
    [ValidateRange(1,64)][int]$Jobs = 4,
    [string]$Flavor,
    [ValidateSet('on','off')][string]$Fsr,
    [string]$FfxMirror,
    [ValidateSet('git','archive')][string]$Fetch,
    [string]$Out,
    [string]$Revision,
    [string]$Progress,
    [ValidateSet('developer','player')][string]$Tests,
    [switch]$SingleTile,
    [switch]$Msaa,
    [ValidateSet(0,2,4)][int]$MsaaSamples,
    [switch]$FixedVblank,
    [switch]$Rov,
    [switch]$Native,
    [switch]$Rtv,
    [switch]$FullRateMirror,
    [switch]$ShadowWorkaround,
    [ValidateSet('native','720p','1080p','1440p','4k')][string]$Display,
    [switch]$Fullscreen,
    [switch]$Supersample,
    [switch]$Info,
    [ValidateSet('1','2','3','4','panel')][string]$ResolutionScale,
    [ValidateRange(30,240)][int]$FrameRate,
    [ValidateSet('auto','on','off')][string]$VSync,
    [string]$Upscale,
    [ValidateSet('s','k')][string]$Entry0,
    [string]$Pad,
    [string]$Keyboard,
    [string]$Wheel,
    [string]$Look,
    [switch]$ShaderDev,
    [string]$TextureDump,
    [string]$TextureReplace,
    [ValidateSet('low','medium','high','extrahigh','ultra','xbox')][string]$Quality,
    [ValidateSet('fm3','fm2')][string]$ModGame,
    [ValidateSet('cars','tracks')][string]$ModContent,
    [string]$ModDisc,
    [string]$ModDisc2,
    [string[]]$Mod
)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This workflow supports Windows x64.' }
# The default build (-Abi mingw) needs llvm-mingw, CMake, Ninja and Python only: -MingwRoot names
# the unpacked llvm-mingw folder, or -Toolchain the portable tools' folder. -Abi msvc, the build
# with Microsoft's libraries kept for the A/B of the switch, runs in the Visual Studio x64
# environment with LLVM 20 (-LLVMRoot).
if ($Abi -eq 'msvc') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ Build Tools and Windows SDK first; see docs/setup.md.' }
    # VsDevCmd calls vswhere.exe by name, so its folder must be on PATH.
    $env:PATH = "$(Split-Path -Parent $vswhere);$env:PATH"
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'Visual Studio C++ tools were not found.' }
    Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    # Without -LLVMRoot, the default LLVM install is used when it exists, so that a clang of
    # another toolchain on PATH (such as llvm-mingw) is not picked up.
    if (!$LLVMRoot) {
        $defaultLLVM = Join-Path $env:ProgramFiles 'LLVM'
        if (Test-Path -LiteralPath (Join-Path $defaultLLVM 'bin/clang.exe')) { $LLVMRoot = $defaultLLVM }
    }
    if ($LLVMRoot) { $env:PATH = "$(Join-Path $LLVMRoot 'bin');$env:PATH" }
}
if ($MingwRoot) { $env:PATH = "$(Join-Path $MingwRoot 'bin');$env:PATH" }
$arguments = @((Join-Path $PSScriptRoot 'pipeline.py'), $Action, '--jobs', "$Jobs")
if ($Abi) { $arguments += @('--abi', $Abi) }
if ($Toolchain) { $arguments += @('--toolchain', $Toolchain) }
if ($Disc1) { $arguments += @('--disc1', $Disc1) }
if ($Disc2) { $arguments += @('--disc2', $Disc2) }
foreach ($path in $Content) { $arguments += @('--content', $path) }
if ($BuildRoot) { $arguments += @('--build-root', $BuildRoot) }
if ($Flavor) { $arguments += @('--flavor', $Flavor) }
if ($Fsr) { $arguments += @('--fsr', $Fsr) }
if ($FfxMirror) { $arguments += @('--ffx-mirror', $FfxMirror) }
if ($Fetch) { $arguments += @('--fetch', $Fetch) }
if ($Out) { $arguments += @('--out', $Out) }
if ($Revision) { $arguments += @('--revision', $Revision) }
if ($Progress) { $arguments += @('--progress', $Progress) }
if ($Tests) { $arguments += @('--tests', $Tests) }
if ($SingleTile) { $arguments += @('--single-tile') }
if ($Msaa) { $arguments += @('--msaa') }
if ($PSBoundParameters.ContainsKey('MsaaSamples')) { $arguments += @('--msaa-samples', "$MsaaSamples") }
if ($FixedVblank) { $arguments += @('--fixed-vblank') }
if ($Rov) { $arguments += @('--rov') }
if ($Native) { $arguments += @('--native') }
if ($Rtv) { $arguments += @('--rtv') }
if ($FullRateMirror) { $arguments += @('--full-rate-mirror') }
if ($ShadowWorkaround) { $arguments += @('--shadow-workaround') }
if ($Display) { $arguments += @('--display', $Display) }
if ($Fullscreen) { $arguments += @('--fullscreen') }
if ($Supersample) { $arguments += @('--supersample') }
if ($Info) { $arguments += @('--info') }
if ($ResolutionScale) { $arguments += @('--resolution-scale', "$ResolutionScale") }
if ($FrameRate) { $arguments += @('--frame-rate', "$FrameRate") }
if ($VSync) { $arguments += @('--vsync', $VSync) }
if ($Upscale) { $arguments += @('--upscale', $Upscale) }
if ($Entry0) { $arguments += @('--entry0', $Entry0) }
if ($Pad) { $arguments += @('--pad', $Pad) }
if ($Keyboard) { $arguments += @('--keyboard', $Keyboard) }
if ($Wheel) { $arguments += @('--wheel', $Wheel) }
if ($Look) { $arguments += @('--look', $Look) }
if ($ShaderDev) { $arguments += @('--shader-dev') }
if ($TextureDump) { $arguments += @('--texture-dump', $TextureDump) }
if ($TextureReplace) { $arguments += @('--texture-replace', $TextureReplace) }
if ($Quality) { $arguments += @('--quality', $Quality) }
if ($ModGame) { $arguments += @('--mod-game', $ModGame) }
if ($ModContent) { $arguments += @('--mod-content', $ModContent) }
if ($ModDisc) { $arguments += @('--mod-disc', $ModDisc) }
if ($ModDisc2) { $arguments += @('--mod-disc2', $ModDisc2) }
foreach ($id in $Mod) { $arguments += @('--mod', $id) }
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw "FM4 workflow failed (exit $LASTEXITCODE). See the build-root logs." }
