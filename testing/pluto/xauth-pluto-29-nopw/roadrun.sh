# fail
ipsec whack --xauthname 'use2' --xauthpass '' --name xauth-road-eastnet-psk --initiate
# pass
ipsec whack --xauthname 'nopw' --xauthpass '' --name xauth-road-eastnet-psk --initiate
sleep 5
ping -n -q -c 4 192.0.2.254
ipsec whack --trafficstatus
# note there should NOT be any incomplete IKE SA attempting to do ModeCFG or EVENT_RETRANSMIT
ipsec showstates
echo done