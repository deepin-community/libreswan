# road is redirected to 1 - 192.1.2.44 - which is down
ipsec whack --impair none
ipsec whack --impair revival --impair delete-on-retransmit
ipsec auto --add road-east
ipsec auto --up road-east
