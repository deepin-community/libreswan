#  should get back authentication failed
ipsec whack --impair suppress-retransmits
ipsec auto --up westnet-eastnet-ikev2
echo done
