# helpers: source this. mon "hmp cmd"; typ "text"; shot out.png
S=$HOME/win31/qemu-monitor.sock
mon(){ printf '%s\n' "$1" | socat -t1 - UNIX-CONNECT:$S | tr -d '\r' | sed 's/\x1b\[[0-9;]*[A-Za-z]//g' | grep -av '^(qemu)\|QEMU .*monitor' ; }
typ(){ local s="$1" i c k; for ((i=0;i<${#s};i++)); do c=${s:i:1}; case $c in
 ' ') k=spc;; '/') k=slash;; '.') k=dot;; ':') k=shift-semicolon;; '>') k=shift-dot;; '\') k=backslash;;
 '-') k=minus;; '_') k=shift-minus;; '=') k=equal;; [A-Z]) k=shift-${c,,};; *) k=$c;; esac; mon "sendkey $k" >/dev/null; done; }
shot(){ local o=${1:-/tmp/claude-1000/shot.png}; mon "screendump /tmp/claude-1000/.sd.ppm" >/dev/null; sleep 0.5; magick /tmp/claude-1000/.sd.ppm "$o"; echo "$o"; }
