# On east this shows the duplicates on west there is nothing.
sed -n -e '/fragment .* duplicate Message ID/ s/ (.*/ (...)/p' /tmp/pluto.log
../../guestbin/ipsec-look.sh
