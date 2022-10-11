/testing/guestbin/swan-prep --x509

ipsec start
../../guestbin/wait-until-pluto-started
ipsec auto --add l2tp-north-to-east-on-east
ipsec auto --add l2tp-distraction-on-east

# ensure that clear text does not get through
iptables -A INPUT  -i eth1 -d 192.1.2.23 -m policy --dir in --pol none -p udp --dport 1701 -j REJECT
iptables -A INPUT -m policy --dir in --pol ipsec -j ACCEPT
iptables -A OUTPUT -o eth1 -s 192.1.2.23 -m policy --dir out --pol none -p udp --sport 1701 -j REJECT
iptables -A OUTPUT -m policy --dir out --pol ipsec -j ACCEPT
(cd /tmp && xl2tpd -D 2>/tmp/xl2tpd.log ) &
../../guestbin/echo.sh
echo done
