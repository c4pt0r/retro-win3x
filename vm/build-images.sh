#!/usr/bin/env bash
# build-images.sh — build vm/images/{win31,win32}.qcow2 from archive.org downloads.
#
#   vm/build-images.sh [win31|win32|all]     (default: all)
#
# win31: English Windows 3.1 + MS-DOS 6.22 (archive.org item "win31vhd")
# win32: Simplified Chinese Windows 3.2 (item "win32c": an installed WINDOWS
#        directory) placed on the same DOS 6.22 disk
#
# Both get W16AGENT.EXE (built from win16dev/tools/w16agent) started from
# WIN.INI, the AUTOEXEC.BAT restart loop it needs, and a bigger DMA buffer.
# These are copyrighted programs: download them only for your own use.
set -euo pipefail

cd -P "$(dirname "$0")"
VM=$PWD
DL=$VM/downloads
OUT=$VM/images
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
export MTOOLS_SKIP_CHECK=1
PART=32256                              # first partition at sector 63

WHAT=${1:-all}
[[ $WHAT =~ ^(win31|win32|all)$ ]] || { sed -n '2,12s/^# \{0,1\}//p' "$0"; exit 1; }

need() { command -v "$1" >/dev/null || { echo "missing: $1" >&2; exit 1; }; }
for c in curl qemu-img mcopy unzip python3 make; do need "$c"; done

fetch() {                               # fetch ITEM FILE -> downloads/FILE
    mkdir -p "$DL"
    [[ -f $DL/$2 ]] && return
    echo "downloading archive.org/$1/$2 ..."
    curl -fL -o "$DL/$2.part" "https://archive.org/download/$1/$2"
    mv "$DL/$2.part" "$DL/$2"
}

make -s -C ../win16dev/tools/w16agent
AGENT=$(cd ../win16dev/tools/w16agent && pwd)/w16agent.exe

fetch win31vhd Win31.vhd
[[ $WHAT == win31 ]] || fetch win32c win32c.zip

# --- helpers ---------------------------------------------------------------

crlf() { python3 -c 'import sys; sys.stdout.buffer.write(sys.stdin.buffer.read().replace(b"\n", b"\r\n"))'; }

# ini_set FILE SECTION KEY VALUE  (byte-level; INI files may be GBK)
ini_set() {
    python3 - "$@" <<'EOF'
import re, sys
path, sec, key, val = sys.argv[1:]
d = open(path, 'rb').read()
sec_b, key_b, val_b = sec.encode(), key.encode(), val.encode()
m = re.search(rb'(?im)^\[' + re.escape(sec_b) + rb'\]\r?\n', d)
if not m:
    d = d.rstrip(b'\r\n') + b'\r\n\r\n[' + sec_b + b']\r\n' + key_b + b'=' + val_b + b'\r\n'
else:
    end = re.search(rb'(?m)^\[', d[m.end():])
    end = m.end() + end.start() if end else len(d)
    body = d[m.end():end]
    k = re.search(rb'(?im)^' + re.escape(key_b) + rb'=[^\r\n]*', body)
    line = key_b + b'=' + val_b
    body = body[:k.start()] + line + body[k.end():] if k else line + b'\r\n' + body
    d = d[:m.end()] + body + d[end:]
open(path, 'wb').write(d)
EOF
}

AUTOEXEC_TAIL='C:\DOS\SMARTDRV.EXE /X
@ECHO OFF
PROMPT $p$g
PATH C:\WINDOWS;C:\DOS
SET TEMP=C:\DOS
:W16LOOP
IF EXIST C:\W16RUN\W16AGENT.NEW COPY C:\W16RUN\W16AGENT.NEW C:\W16RUN\W16AGENT.EXE
IF EXIST C:\W16RUN\W16AGENT.NEW DEL C:\W16RUN\W16AGENT.NEW
IF EXIST C:\W16RUN\RESTART.FLG DEL C:\W16RUN\RESTART.FLG
WIN
IF EXIST C:\W16RUN\RESTART.FLG GOTO W16LOOP
'

# common setup on a raw disk: agent, AUTOEXEC loop, WIN.INI, SYSTEM.INI
setup_disk() {                          # setup_disk RAW AUTOEXEC_TEXT
    local C=$1@@$PART
    mmd -i "$C" ::/W16RUN 2>/dev/null || true
    mcopy -i "$C" -o "$AGENT" ::/W16RUN/W16AGENT.EXE
    printf '%s' "$2" | crlf > "$WORK/autoexec.bat"
    mcopy -i "$C" -o "$WORK/autoexec.bat" ::/AUTOEXEC.BAT
    mcopy -i "$C" -o ::/WINDOWS/WIN.INI "$WORK/win.ini"
    ini_set "$WORK/win.ini" windows load 'C:\W16RUN\W16AGENT.EXE'
    mcopy -i "$C" -o "$WORK/win.ini" ::/WINDOWS/WIN.INI
    mcopy -i "$C" -o ::/WINDOWS/SYSTEM.INI "$WORK/system.ini"
    ini_set "$WORK/system.ini" 386Enh DMABufferSize 64
    mcopy -i "$C" -o "$WORK/system.ini" ::/WINDOWS/SYSTEM.INI
}

finish() {                              # finish RAW NAME
    mkdir -p "$OUT"
    qemu-img convert -c -O qcow2 "$1" "$OUT/$2.qcow2.new"
    mv "$OUT/$2.qcow2.new" "$OUT/$2.qcow2"
    echo "built $OUT/$2.qcow2 ($(du -h "$OUT/$2.qcow2" | cut -f1))"
}

# --- win31 -----------------------------------------------------------------

if [[ $WHAT != win32 ]]; then
    qemu-img convert -O raw "$DL/Win31.vhd" "$WORK/win31.raw"
    # keep the image's Russian keyboard/code page lines, then our loop
    mcopy -i "$WORK/win31.raw@@$PART" ::/AUTOEXEC.BAT "$WORK/orig.bat"
    extra=$(tr -d '\r' < "$WORK/orig.bat" | grep -iE '^(MODE|CHCP|KEYB) ' || true)
    tail=${AUTOEXEC_TAIL/:W16LOOP/$extra
:W16LOOP}
    setup_disk "$WORK/win31.raw" "$tail"
    finish "$WORK/win31.raw" win31
fi

# --- win32 -----------------------------------------------------------------

if [[ $WHAT != win31 ]]; then
    qemu-img convert -O raw "$DL/Win31.vhd" "$WORK/win32.raw"
    C=$WORK/win32.raw@@$PART
    mdeltree -i "$C" ::/WINDOWS
    unzip -q -o "$DL/win32c.zip" -d "$WORK/w32c"
    mcopy -i "$C" -s "$WORK/w32c/WINDOWS" ::/
    printf 'DEVICE=C:\\DOS\\HIMEM.SYS\nDOS=HIGH\nFILES=40\nBUFFERS=20\nSTACKS=9,256\n' | crlf > "$WORK/config.sys"
    mcopy -i "$C" -o "$WORK/config.sys" ::/CONFIG.SYS
    setup_disk "$WORK/win32.raw" "${AUTOEXEC_TAIL/SET TEMP=C:\\DOS/SET TEMP=C:\\WINDOWS\\TEMP}"
    finish "$WORK/win32.raw" win32
fi
