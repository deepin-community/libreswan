/testing/guestbin/swan-prep --46

ip -4 route
ip -6 route

# %any
ip -4 route get to 0.0.0.0
ip -6 route get to ::

# gateway
ip -4 route get to 192.1.3.254
ip -6 route get to 2001:db8:1:3::254

# peer
ip -4 route get to 192.1.2.23
ip -6 route get to 2001:db8:1:2::23

# simple case, peer known

ipsec addconn --verbose ipv4-src 2>&1 | grep -e '^resolving ' -e '^  [^ ]'
ipsec addconn --verbose ipv6-src 2>&1 | grep -e '^resolving ' -e '^  [^ ]'

# now with no forced host

ipsec addconn --verbose ipv4-default 2>&1 | grep -e '^resolving ' -e '^  [^ ]'
ipsec addconn --verbose ipv6-default 2>&1 | grep -e '^resolving ' -e '^  [^ ]'

# newoe case, peer is group (see newoe-20-ipv6)

ipsec addconn --verbose ipv4-src-group 2>&1 | grep -e '^resolving ' -e '^  [^ ]'
ipsec addconn --verbose ipv6-src-group 2>&1 | grep -e '^resolving ' -e '^  [^ ]'

# re-adding while route is up (see ikev1-hostpair-02)

ip -4 address add dev eth0     192.0.2.1/32
ip -6 address add dev eth0  2001:db8:0:2::1 nodad
ip -4 route   add dev eth0       192.1.2.23 via       192.1.3.254 src       192.0.2.1
ip -6 route   add dev eth0 2001:db8:1:2::23 via 2001:db8:1:3::254 src 2001:db8:0:2::1
ip -4 route
ip -6 route

ipsec addconn --verbose ipv4-src-group 2>&1 | grep -e '^resolving ' -e '^  [^ ]'
ipsec addconn --verbose ipv6-src-group 2>&1 | grep -e '^resolving ' -e '^  [^ ]'

ip -4 route   del dev eth0       192.1.2.23 via       192.1.3.254 src       192.0.2.1
ip -6 route   del dev eth0 2001:db8:1:2::23 via 2001:db8:1:3::254 src 2001:db8:0:2::1
ip -4 address del dev eth0     192.0.2.1/32
ip -6 address del dev eth0  2001:db8:0:2::1
ip -4 route
ip -6 route

# messed up default (see ipv6-addresspool-05-dual-stack)

ip -6 route del default
ip -6 route add default dev eth0 via fe80::1000:ff:fe32:64ba
ip -6 route

ipsec addconn --verbose ipv6-src 2>&1 | grep -e '^resolving ' -e '^  [^ ]'
