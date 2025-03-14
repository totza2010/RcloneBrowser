# Install RcloneBrowser on Windows

# Set execution policy to allow script execution
Set-ExecutionPolicy Bypass -Scope Process -Force

# Install Chocolatey if not installed
if (-Not (Get-Command choco -ErrorAction SilentlyContinue)) {
    Write-Host "Installing Chocolatey..."
    Set-ExecutionPolicy Bypass -Scope Process -Force; `
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; `
    iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
}

# Install dependencies
choco install -y git cmake visualstudio2019buildtools qt5

# Clone repository
if (-Not (Test-Path "RcloneBrowser")) {
    git clone https://github.com/totza2010/RcloneBrowser.git
}

# Build
Set-Location RcloneBrowser
mkdir -Force build
Set-Location build
cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_CONFIGURATION_TYPES="Release" -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" ..
cmake --build . --config Release
& "C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe" --no-translations --no-angle --no-compiler-runtime --no-svg ".\Release\RcloneBrowser.exe"

Write-Host "✅ Installation complete!"
