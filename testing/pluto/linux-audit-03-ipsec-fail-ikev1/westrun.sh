# ipsec fail tests
# See description of limitations of this test
ipsec auto --up ikev1-ipsec-fail #retransmits
ipsec auto --delete ikev1-ipsec-fail
ipsec auto --up ikev1-aggr-ipsec-fail #retransmits
ipsec auto --delete ikev1-aggr-ipsec-fail
echo done
