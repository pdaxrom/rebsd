#!/bin/sh
#
# Local TELNET smoke.  Starts a one-shot telnetd on loopback with an explicit
# shell backend, connects with the in-tree telnet client, runs one command, and
# exits.  The normal telnetd default still uses /bin/login.
#

echo "telnet-smoke diag v1"

cd /tmp || exit 1
rm -f telnet-smoke.out telnet-crypto-smoke.out

echo "step 1: start telnetd"
/usr/libexec/telnetd -1 -s /bin/sh -p 2323 &
server=$!
sleep 1

echo "step 2: telnet shell"
telnet -c 'echo telnet-smoke-ok; exit' 127.0.0.1 2323 >telnet-smoke.out

echo "step 3: validate output"
grep 'telnet-smoke-ok' telnet-smoke.out >/dev/null || {
        cat telnet-smoke.out
        kill $server >/dev/null 2>&1
        exit 1
}

wait $server >/dev/null 2>&1
rm -f telnet-smoke.out

echo "step 4: start encrypted telnetd"
/usr/libexec/telnetd -1 -K retrobsd-smoke-key -s /bin/sh -p 2324 &
server=$!
sleep 1

echo "step 5: encrypted telnet shell"
telnet -K retrobsd-smoke-key -c 'echo telnet-crypto-smoke-ok; exit' 127.0.0.1 2324 >telnet-crypto-smoke.out

echo "step 6: validate encrypted output"
grep 'telnet-crypto-smoke-ok' telnet-crypto-smoke.out >/dev/null || {
        cat telnet-crypto-smoke.out
        kill $server >/dev/null 2>&1
        exit 1
}

wait $server >/dev/null 2>&1
rm -f telnet-crypto-smoke.out
echo "telnet-smoke ok"
