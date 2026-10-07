#!/bin/sh
# Floppy A: = exchange.img (managed from Linux with `w31x`)
# COM1 = com1.sock, talks to W16AGENT.EXE in Windows (see `w16ctl`)
# COM2 = com2.sock, CHAT.EXE <-> w16chatd <-> pi (started here if not running)
cd -P "$(dirname "$0")"
PATH="$(cd ../bin && pwd):$PATH"   # repo tools (w31x, w16chatd, ...)
# usage: run.sh [win32|win31]   (default win32 = Simplified Chinese Windows 3.2)
# images/NAME.qcow2 = base image built by ./build-images.sh (never written);
# disks/NAME.qcow2  = this machine's working disk, created on first run as an
#                     overlay on the base image (delete it to reset the VM).
NAME=${1:-win32}
DISK=disks/$NAME.qcow2
if [ ! -f "$DISK" ]; then
    [ -f "images/$NAME.qcow2" ] || { echo "no image images/$NAME.qcow2 — run vm/build-images.sh first" >&2; exit 1; }
    mkdir -p disks
    qemu-img create -q -f qcow2 -b "../images/$NAME.qcow2" -F qcow2 "$DISK"
fi
[ -f exchange.img ] || w31x init
pgrep -if 'python.*w16chatd' >/dev/null || (nohup w16chatd -- --model "${W16CHAT_MODEL:-openai/gpt-6-luna}" >>w16chatd.log 2>&1 </dev/null &)
case $(uname) in Darwin) DISPLAY_OPT=cocoa,zoom-to-fit=on,full-grab=on ;; *) DISPLAY_OPT=gtk ;; esac
exec qemu-system-i386 -machine pc -cpu 486 -m 32 \
  -drive file="$DISK",format=qcow2,if=ide \
  -drive file=exchange.img,format=raw,if=floppy,index=0 \
  -boot order=c \
  -vga cirrus -device sb16 -rtc base=localtime \
  -monitor unix:qemu-monitor.sock,server,nowait \
  -serial unix:com1.sock,server,nowait \
  -serial unix:com2.sock,server,nowait \
  -display "$DISPLAY_OPT" -name "Windows $NAME"
