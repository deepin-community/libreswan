# match: ip (|-[46]) xfrm policy ...

/^ ip xfrm policy$/ b match
/^ ip -4 xfrm policy$/ b match
/^ ip -6 xfrm policy$/ b match
b end

:match

  # print and read next line
  n
  /^[a-z]* #/ b end

  / spi 0x00000000 /! s/ spi 0x[^ ]* / spi 0xSPISPI /g
  s/ reqid [1-9][0-9]* / reqid REQID /g

  # dir ... priority 2080718 ptype ...
  s/ priority [1-9][0-9]* ptype / priority PRIORITY ptype /

b match

:end
