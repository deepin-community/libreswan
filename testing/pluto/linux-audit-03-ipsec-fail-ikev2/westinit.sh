/testing/guestbin/swan-prep
setenforce 1
ipsec start
../../guestbin/wait-until-pluto-started
ipsec auto --add ikev2-ipsec-fail
#ipsec whack --impair delete-on-retransmit
echo "initdone"
