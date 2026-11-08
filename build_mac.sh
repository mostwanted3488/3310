#!/bin/bash
# Compile 3310 (VST3 + AU, universal Apple Silicon + Intel) sur ton Mac et l'installe pour FL Studio.
# Prérequis (une seule fois) :  xcode-select --install   et   brew install cmake
# Usage : double-clic impossible, ouvre le Terminal dans ce dossier et lance :  ./build_mac.sh
set -e
cd "$(dirname "$0")"

command -v cmake >/dev/null 2>&1 || { echo "cmake manquant : brew install cmake"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Outils Xcode manquants : xcode-select --install"; exit 1; }

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target N3310_VST3 N3310_AU --parallel

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"
rm -rf "$VST3_DIR/3310.vst3" "$AU_DIR/3310.component"
ditto "build/N3310_artefacts/Release/VST3/3310.vst3" "$VST3_DIR/3310.vst3"
ditto "build/N3310_artefacts/Release/AU/3310.component" "$AU_DIR/3310.component"

# plugin non signé : on enlève la quarantaine et on signe en "ad hoc" pour que macOS le charge
xattr -cr "$VST3_DIR/3310.vst3" "$AU_DIR/3310.component" 2>/dev/null || true
codesign --force --deep --sign - "$VST3_DIR/3310.vst3" 2>/dev/null || true
codesign --force --deep --sign - "$AU_DIR/3310.component" 2>/dev/null || true

echo
echo "OK : 3310 installé dans $VST3_DIR et $AU_DIR"
echo "Dans FL Studio : Options > Manage plugins > Find installed plugins (rescan)."
