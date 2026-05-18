#!/bin/bash
set -e

echo Starting install.sh

INSTALL_PATH="/opt/BeefLang"

if [ "$#" -gt 0 ]; then
    INSTALL_PATH=$1
fi

echo "Installing Beef to $INSTALL_PATH..."

### COMPILER ###

find jbuild/Release/bin/*.a -type f -exec install -Dm644 -t "${INSTALL_PATH}/bin/" "{}" \;
find jbuild/Release/bin/*.so -type f -exec install -Dm644 -t "${INSTALL_PATH}/bin/" "{}" \;
find jbuild_d/Debug/bin/*.a -type f -exec install -Dm644 -t "${INSTALL_PATH}/bin/" "{}" \;
find jbuild_d/Debug/bin/*.so -type f -exec install -Dm644 -t "${INSTALL_PATH}/bin/" "{}" \;

install -Dm755 -t "${INSTALL_PATH}/bin/" "IDE/dist/BeefBuild"
install -Dm755 -t "${INSTALL_PATH}/bin/" "IDE/dist/BeefBuild_d"
install -Dm644 "IDE/dist/BeefConfig_install.toml" "${INSTALL_PATH}/bin/BeefConfig.toml"

find BeefLibs -type f -exec install -Dm644 "{}" "${INSTALL_PATH}/{}" \;

install -Dm644 -t "${INSTALL_PATH}/" "LICENSE.TXT"
install -Dm644 -t "${INSTALL_PATH}/" "LICENSES.TXT"
install -Dm644 -t "${INSTALL_PATH}/" "README.md"

### IDE ###

if [ -f "IDE/dist/BeefIDE" ]; then
    install -Dm755 -t "${INSTALL_PATH}/bin/" "IDE/dist/BeefIDE"
    install -Dm755 -t "${INSTALL_PATH}/bin/" "IDE/dist/BeefIDE_d"

    install -Dm644 -t "${INSTALL_PATH}/bin/" "IDE/dist/en_US.aff"
    install -Dm644 -t "${INSTALL_PATH}/bin/" "IDE/dist/en_US.dic"

    find IDE/dist/fonts -type f -exec install -Dm644 -t "${INSTALL_PATH}/bin/fonts" "{}" \;
    find IDE/dist/images -type f -exec install -Dm644 -t "${INSTALL_PATH}/bin/images" "{}" \;
    find IDE/dist/shaders -type f -exec install -Dm644 -t "${INSTALL_PATH}/bin/shaders" "{}" \;
fi

echo "Installed Beef successfully."