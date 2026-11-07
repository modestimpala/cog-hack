#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Download + verify + extract the AngelScript SDK into third_party/sdk (not committed).
set -e
cd "$(dirname "$0")/.."
URL=https://www.angelcode.com/angelscript/sdk/files/angelscript_2.38.0.zip
SHA=b33b5dbcda10317ef67d628353d83246984ce6fcac102d4dc2aed121eba52e6f
mkdir -p third_party/dl
ZIP=third_party/dl/angelscript_2.38.0.zip
[ -f "$ZIP" ] || curl -sSfL -o "$ZIP" "$URL"
echo "$SHA  $ZIP" | sha256sum -c -
rm -rf third_party/sdk
unzip -q "$ZIP" -d third_party/
echo "AngelScript 2.38.0 ready in third_party/sdk"
