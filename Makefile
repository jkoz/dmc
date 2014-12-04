include config.mk

ifeq ($(HAVE_SSL),1)
SSL_LIBS=`pkg-config libssl --libs`
CFLAGS+=`pkg-config libssl --cflags`
endif

CFLAGS+=-DHAVE_SSL=${HAVE_SSL}
CFLAGS+=-DVERSION=\"${VERSION}\"
CFLAGS+=-DPREFIX=\"${PREFIX}\"
pfx=${DESTDIR}/${PREFIX}

BINS=dmc dmc-mbox dmc-smtp dmc-pop3 dmc-imap4 dmc-pack dmc-filter dmc-mbox

all: config.h ${BINS}

config.h:
	cp config.def.h config.h

dmc: dmc.o
	${CC} dmc.o -o dmc

dmc-smtp: smtp.o
	${CC} ${LDFLAGS} smtp.o -o dmc-smtp -lresolv

dmc-mbox: mbox.o
	${CC} ${LDFLAGS} mbox.o -o dmc-mbox

dmc-pack: pack.o
	${CC} ${LDFLAGS} pack.o -o dmc-pack

dmc-filter: filter.o
	${CC} ${LDFLAGS} filter.o -o dmc-filter

sock.tmp: sock.o
	rm -f pop3.o imap4.o
	@:> sock.tmp

dmc-pop3: sock.tmp pop3.o
	${CC} ${LDFLAGS} ${SSL_LIBS} pop3.o -o dmc-pop3

dmc-imap4: sock.tmp imap4.o
	${CC} ${LDFLAGS} ${SSL_LIBS} imap4.o -o dmc-imap4

install:
	chmod +x dmc dmc-tag dmc-mdir
	mkdir -p ${pfx}/share/man/man1 ${pfx}/bin
	cp -f dmc.1 ${pfx}/share/man/man1
	cp -f dmc ${pfx}/bin
	cp -f dmc.sh ${pfx}/bin
	cp -f dmc-tag ${pfx}/bin
	cp -f dmc-smtp ${pfx}/bin
	cp -f dmc-pop3 ${pfx}/bin
	cp -f dmc-imap4 ${pfx}/bin
	cp -f dmc-pack ${pfx}/bin
	cp -f dmc-filter ${pfx}/bin
	cp -f dmc-mbox ${pfx}/bin
	cp -f dmc-mdir ${pfx}/bin

uninstall:
	rm -f ${pfx}/bin/dmc
	rm -f ${pfx}/bin/dmc-*
	rm -f ${pfx}/share/man/man1/dmc.1

clean:
	rm -f ${BINS} *.o sock.tmp

dist:
	rm -rf dmc-${VERSION}
	hg clone . dmc-${VERSION}
	rm -rf dmc-${VERSION}/.hg
	tar czvf dmc-${VERSION}.tar.gz dmc-${VERSION}
	rm -rf dmc-${VERSION}

loc:
	sloccount .
