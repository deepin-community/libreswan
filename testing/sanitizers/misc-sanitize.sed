#s/^\[[0-9]\]* [0-9]*$/[X] XXXX/
# filter out the backgrounding of tcpdump
# tcpdump -i lo -n -c 6 2> /dev/null &
# [1] 1652
/^ tcpdump .*\&$/ {N; s/^ tcpdump \(.*\&\)\n\[[0-9]*\] [0-9]*$/ tcpdump \1\n[B] PID/g}
# why not just all backgrounding
s/^\[[0-9]\] [0-9]*$/[x] PID/
# nc -4 -l 192.1.2.23 222 &
#[1] 2209
/^ nc .*\&$/ {N; s/^ nc \(.*\&\)\n\[[0-9]*\] [0-9]*$/ nc \1\n[B] PID/g}
/^ (cd \/tmp \&\& xl2tpd.*/ {N; s/^ \((cd \/tmp \&\& xl2tpd.*\)\n\[[0-9]*\] [0-9]*$/ \1\n[B] PID/g}
# versions of tools used
s/SSH-2.0-OpenSSH_.*$/SSH-2.0-OpenSSH_XXX/
/^ *Electric Fence.*$/d
/^.*anti-replay context:.*$/d

s/add bare shunt 0x[^ ]* /add bare shunt 0xPOINTER /
s/delete bare shunt 0x[^ ]* /delete bare shunt 0xPOINTER /
s/comparing bare shunt 0x[^ ]* /comparing bare shunt 0xPOINTER /

s/ike-scan \(.*\) with/ike-scan XX with/
s/Ending ike-scan \(.*\):/ Ending ike-scan XX:/
s/conntrack v[0-9]*\.[0-9]*\.[0-9]* /conntrack vA.B.C /
s/ip_vti0@NONE: <NOARP> mtu [0-9]* /ip_vti0@NONE: <NOARP> mtu XXXX /
# this prevents us seeing race conditions between namespaces / kvm
/^.*Terminated.*ip -s xfrm monitor.*$/d
# sshd on fedora 30 and 31 have slightly different error msgs
s/^Protocol mismatch\.$/Invalid SSH identification string./g
/^.*for ASN.1 blob for method.*$/d
# nss picks up softhsm/opendnssec token?
/^.* for token "OpenDNSSEC".*$/d
/^Relabeled \/testing.*$/d
# some things are different on Debian/Ubuntu, and we dont really need to see those for testing
/000 nssdir=.*$/d
/000 dnssec-rootkey-file=.*$/d

# timing info from the log
s/last_contact=0->[0-9]*\.[0-9]*/last_contact=0->XX.XXX/g
s/last_contact=[0-9]*\.[0-9]*->[0-9]*\.[0-9]*/last_contact=XX.XXX->XX.XXX/g
s/last_contact=[0-9]*\.[0-9]*/last_contact=XX.XXX/g

# TCP sockets
s/ socket [0-9][0-9]*: / socket XX: /g

s/encap type 7 sport/encap type espintcp sport/g
s/unbound-control.[0-9]*:[0-9]*./unbound-control[XXXXXX:X] /g 
s/ping: connect: Network is unreachable/connect: Network is unreachable/g
# softhsm - pkcs-uri ephemerals
s/serial=[^;]*;token=libreswan/serial=XXXXXXXX;token=libreswan/g
s/and is reassigned to slot .*$/and is reassigned to slot XXXXX/g

s/tcpdump: listening on .*$/tcpdump: listening on INTERFACE DETAILS/

# prevent stray packets from changing counters
s/\t 00000000 00000000 00000000 .*$/\t 00000000 00000000 00000000 XXXXXXXX /g
s/\t seq-hi 0x0, seq [^,]*, oseq-hi 0x0, oseq .*$/\t seq-hi 0x0, seq 0xXX, oseq-hi 0x0, oseq 0xXX/g