# road is redirected to 2 - 192.1.2.45 - which is up
ipsec whack --impair none
ipsec whack --impair revival --impair suppress-retransmits
ipsec auto --up road-east
../../guestbin/ping-once.sh --up 192.0.2.254
ipsec whack --trafficstatus
ipsec auto --delete road-east
