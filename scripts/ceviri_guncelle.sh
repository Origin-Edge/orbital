#!/usr/bin/env bash
# Arayüz çevirisini günceller: kaynaktaki yeni qsTr()/translate() metinlerini i18n/orbital_en.ts'e ekler
# ve .qm'i derler. Yeni metinlerin İngilizcesini Qt Linguist ile (linguist6 i18n/orbital_en.ts) gir.
set -euo pipefail
cd "$(dirname "$0")/.."
LUPDATE=$(command -v lupdate6 || command -v lupdate)
LRELEASE=$(command -v lrelease6 || command -v lrelease)
"$LUPDATE" -no-obsolete -source-language tr_TR -target-language en_US resources/qml src -ts i18n/orbital_en.ts
"$LRELEASE" i18n/orbital_en.ts -qm i18n/orbital_en.qm
