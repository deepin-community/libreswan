BSD_VARIANT=freebsd
# sketch out pkgsrc
PKG_BASE ?= /usr/local

USERLAND_CFLAGS += -DHAS_SUN_LEN
USERLAND_CFLAGS += -DNEED_SIN_LEN

USERLAND_INCLUDES += -I$(PKG_BASE)/include

USERLAND_LDFLAGS += -L$(PKG_BASE)/lib -Wl,-rpath,$(PKG_BASE)/lib

# NSS includes more than needed in ldflags
NSS_LDFLAGS = -L$(PKG_BASE)/lib/nss -Wl,-rpath,$(PKG_BASE)/lib/nss -lnss3 -lfreebl3 -lssl3
NSPR_LDFLAGS = -L$(PKG_BASE)/lib/nspr -Wl,-rpath,$(PKG_BASE)/lib/nspr -lnspr4

USE_BSDKAME = true
USE_PFKEYV2 = true

USE_LIBCAP_NG = false
USE_UNBOUND_EVENT_H_COPY = true
USE_PTHREAD_SETSCHEDPRIO = false

INITSYSTEM=rc.d

# not /run/pluto
FINALRUNDIR=/var/run/pluto

# PREFIX = /usr/local from mk/config.mk
FINALSYSCONFDIR=$(PREFIX)/etc
FINALNSSDIR=$(PREFIX)/etc/ipsec.d
FINALEXAMPECONFDIR=$(PREFIX)/share/examples/libreswan
