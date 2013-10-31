RELEASE=3.1

PACKAGE=spiceterm
VERSION=1.0
PACKAGERELEASE=1

ARCH:=$(shell dpkg-architecture -qDEB_BUILD_ARCH)
GITVERSION:=$(shell cat .git/refs/heads/master)

DEB=${PACKAGE}_${VERSION}-${PACKAGERELEASE}_${ARCH}.deb

PROGRAMS=spiceterm

HEADERS=translations.h event_loop.h glyphs.h spiceterm.h keysyms.h
SOURCES=screen.c event_loop.c input.c spiceterm.c auth-pve.c

#export G_MESSAGES_DEBUG=all 
#export SPICE_DEBUG=1

all: ${PROGRAMS}

spiceterm: ${SOURCES} ${HEADERS} spiceterm.c 
	gcc -Werror -Wall -Wtype-limits ${SOURCES} -g -O2 -o $@ -lutil $(shell pkg-config) $(shell pkg-config --cflags --libs gthread-2.0,spice-protocol,spice-server)

genfont: genfont.c
	gcc -g -O2 -o $@ genfont.c -Wall -D_GNU_SOURCE -lz

keysyms.h: genkeysym.pl
	./genkeysym.pl >$@

.PHONY: glyphs
glyphs: genfont
	./genfont > glyphs.h

.PHONY: install
install: spiceterm
	mkdir -p ${DESTDIR}/usr/share/doc/${PACKAGE}
	install -m 0644 copyright ${DESTDIR}/usr/share/doc/${PACKAGE}
	mkdir -p ${DESTDIR}/usr/share/man/man1
	mkdir -p ${DESTDIR}/usr/bin
	install -s -m 0755 spiceterm ${DESTDIR}/usr/bin

.PHONY: deb
${DEB} deb:
	make clean
	rsync -a . --exclude build build
	echo "git clone git://git.proxmox.com/git/spiceterm.git\\ngit checkout ${GITVERSION}" > build/debian/SOURCE
	cd build; dpkg-buildpackage -rfakeroot -b -us -uc
	lintian ${DEB}

.PHONY: dinstall
dinstall: ${DEB}
	dpkg -i ${DEB}

.PHONY: test
test: spiceterm
	./spiceterm --noauth --keymap de & remote-viewer spice://localhost?tls-port=5900
	#G_MESSAGES_DEBUG=all SPICE_DEBUG=1 SPICE_TICKET=test ./spiceterm & G_MESSAGES_DEBUG=all SPICE_DEBUG=1 remote-viewer --debug 'spice://localhost?tls-port=5900' --spice-ca-file /etc/pve/pve-root-ca.pem --spice-secure-channels=all

.PHONY: distclean
distclean: clean

.PHONY: clean
clean:
	rm -rf *~ ${PROGRAMS} build *.deb *.changes
