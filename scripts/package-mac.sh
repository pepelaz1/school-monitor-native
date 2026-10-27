#!/bin/sh
set -eu
cmake --build build --target SchoolMonitor --parallel 6
mkdir -p build/SchoolMonitor.app/Contents/Resources/licenses dist
cp -R licenses/. build/SchoolMonitor.app/Contents/Resources/licenses/
cp LICENSE build/SchoolMonitor.app/Contents/Resources/licenses/SchoolMonitor-MIT.txt
codesign --force --deep --sign - build/SchoolMonitor.app
ditto -c -k --sequesterRsrc --keepParent build/SchoolMonitor.app dist/SchoolMonitor-Native-0.1.0-mac-arm64.zip
