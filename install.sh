#!/bin/bash

set -e  # หยุดการทำงานหากมีข้อผิดพลาด

detect_os() {
    case "$(uname -s)" in
        Linux*)   OS="linux" ;;
        Darwin*)  OS="macos" ;;
        CYGWIN*|MINGW32*|MSYS*|MINGW*) OS="windows" ;;
        *)        OS="unknown" ;;
    esac
}

install_linux() {
    if [[ -f /etc/debian_version ]]; then
        sudo apt update && sudo apt -y install git g++ cmake make qtdeclarative5-dev qtmultimedia5-dev
    elif [[ -f /etc/redhat-release ]]; then
        if command -v dnf &> /dev/null; then
            sudo dnf -y install git g++ cmake make qt5-qtdeclarative-devel qt5-qtmultimedia-devel
        else
            sudo yum -y install git gcc-c++ cmake make qt5-qtdeclarative qt5-qtmultimedia-devel
        fi
    elif [[ -f /etc/arch-release ]]; then
        sudo pacman -Sy --noconfirm --needed git gcc cmake make qt5-declarative qt5-multimedia
    elif [[ -f /etc/SuSE-release ]]; then
        sudo zypper ref && sudo zypper --non-interactive install git cmake make gcc-c++ libQt5Core-devel libQt5Widgets-devel libQt5Network-devel libqt5-qtmultimedia-devel
    else
        echo "Unsupported Linux distribution"
        exit 1
    fi
}

install_macos() {
    if ! command -v brew &> /dev/null; then
        echo "Homebrew not found. Installing Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi
    brew install git cmake qt5 rclone
}

install_windows() {
    echo "Windows detected. Please run the following PowerShell script manually:"
    echo ""
    echo "git clone https://github.com/totza2010/RcloneBrowser.git"
    echo "cd RcloneBrowser"
    echo "mkdir build"
    echo "cd build"
    echo 'cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_CONFIGURATION_TYPES="Release" -DCMAKE_PREFIX_PATH="C:\Qt\5.13.2\msvc2017_64" ..'
    echo 'cmake --build . --config Release'
    echo 'C:\Qt\5.13.2\msvc2017_64\bin\windeployqt.exe --no-translations --no-angle --no-compiler-runtime --no-svg ".\build\Release\RcloneBrowser.exe"'
}

build_rclone_browser() {
    echo "Cloning RcloneBrowser..."
    git clone https://github.com/totza2010/RcloneBrowser.git || (cd RcloneBrowser && git pull)
    cd RcloneBrowser

    echo "Building RcloneBrowser..."
    mkdir -p build && cd build
    if [[ $OS == "macos" ]]; then
        cmake .. -DCMAKE_PREFIX_PATH:PATH=/usr/local/opt/qt
    else
        cmake ..
    fi
    make -j$(nproc)

    echo "Installing RcloneBrowser..."
    sudo make install
    cd
}

main() {
    detect_os
    case $OS in
        "linux")
            install_linux
            build_rclone_browser
            ;;
        "macos")
            install_macos
            build_rclone_browser
            ;;
        "windows")
            install_windows
            ;;
        *)
            echo "Unsupported OS"
            exit 1
            ;;
    esac
    echo "✅ RcloneBrowser installation complete!"
}

main
