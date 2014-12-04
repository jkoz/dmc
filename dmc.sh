#!/bin/sh
# dmc - dynamic mail client
# See LICENSE file for copyright and license details.

VERSION="0.1"
HELP="Usage: dmc [-hv] [-e acc] [-a addr] [-m addr subj] [-A file] [-s msg] [action]"

[ -z "${EDITOR}" ] && EDITOR=vim
mkdir -p ~/.dmc/mail
mkdir -p ~/.dmc/tmp
mkdir -p ~/.dmc/acc
[ -e ~/.dmc/acc.default ] && . ~/.dmc/acc.default

chkcfg () {
	if [ -z "${NAME}" ]; then
		echo "No selected mail account. Use 'dmc -e'"
		exit 1
	fi
}

acc_daemon () {
	chkcfg
	LOCK=~/.dmc/tmp/${NAME}.lock
	INPUT=~/.dmc/tmp/${NAME}.input
	OUTPUT=~/.dmc/tmp/${NAME}.output

	echo "Starting $NAME account $PROTOCOL daemon..."

	rm -f "${LOCK}" "${INPUT}" "${OUTPUT}"
	mkfifo "${LOCK}" "${INPUT}"

	# wait 1 connect message and 1 login message
	(head -n 2 ${LOCK} ; dmc_cmd "login ${USER} ${PASS}" ; dmc_cmd "ls") &
	(while : ; do cat ${INPUT} 2> /dev/null ; done) | \
		dmc-${PROTOCOL} ${HOST} ${PORT} ${SSL} 2> ${OUTPUT} > ${LOCK}
	rm -f "${LOCK}" "${INPUT}" "${OUTPUT}"
}

dmc_cmd () {
	[ -z "$1" ] && return
	echo "$@" > ~/.dmc/tmp/${NAME}.input
	head -n 1 ~/.dmc/tmp/${NAME}.lock > /dev/stderr
	cat ~/.dmc/tmp/${NAME}.output
}

start_account_daemons () {
	chkcfg
	i=0
	for a in ~/.dmc/acc/* ; do
		( . $a ; acc_daemon ) &
		i=$(($i+1))
		sleep 1
	done
	if [ $i = 0 ]; then
		echo "No accounts defined in ~/.dmc/acc"
		exit 1
	fi
}

print_account_template () {
	echo "NAME='test'"
	echo "SSL=0"
	echo "LIMIT=50 # get only 50 mails for each folder"
	echo "PROTOCOL='pop3' # imap4"
	echo "HOST=serverhost.com"
	echo "PORT=110"
	echo "#SEND=acc-name"
	echo "SEND=!msmtp"
	echo "ONLINE=0"
	echo "MAIL=username@domain"
	echo "USER='username'"
	echo "PASS='password'"
}

edit_message () {
	chkcfg
	OUTDIR=~/.dmc/box/${NAME}/out
	mkdir -p ${OUTDIR}
	OUT="`mktemp ${OUTDIR}/mail.XXXXXX`"
	echo "X-Mailer: dmc v${VERSION}" > $OUT
	echo "From: ${MAIL}" >> $OUT
	echo "To: ${TO}" >> $OUT
	echo "Subject: ${SUBJECT}" >> $OUT
	echo "" >> $OUT
	echo "" >> $OUT
	if [ -e ~/.dmc/box/${NAME}/signature ]; then
		echo "---" >> $OUT
		cat ~/.dmc/box/${NAME}/signature >> $OUT
	else
		if [ -e ~/.dmc/signature ]; then
			echo "---" >> $OUT
			cat ~/.dmc/signature >> $OUT
		fi
	fi
	${EDITOR} ${OUT}
	if [ -z "`cat ${OUT}`" ]; then
		echo "Mail aborted"
		rm -f "${OUT}"
	else
		ln -fs "${OUT}" ~/.dmc/mail.last
	fi
}

add_attachment () {
	chkcfg
	OUT="`readlink ~/.dmc/mail.last`"
	if [ -z "${OUT}" ]; then
		echo "No ~/.dmc/mail.last found. 'dmc -m' or manual symlink required."
	else
		mkdir -p "${OUT}.d"
		if [ -f "$1" ]; then
			FILE="`basename \"$1\"`"
			ln -fs "$1" "${OUT}.d/${FILE}"
		else
			echo "Cannot find \"$1\""
		fi
	fi
}

send_message () {
	chkcfg
	FILE=$1
	if [ ! -e "${FILE}" ]; then
		echo "Cannot find '${FILE}'"
		exit 1
	fi
	# TODO: find better name for the auto mode
	if [ -e "${FILE}.d" ]; then
		TMP="`mktemp ~/.dmc/tmp/mail.XXXXXX`"
		cat $FILE | dmc-pack `ls ${FILE}.d/*` > $TMP 
	else
		TMP=$FILE
	fi
	if [ "${SEND}" = "!msmtp" ]; then
		TO="`dmc-filter -v To: < $FILE`"
		SJ="`dmc-filter -v Subject: < $FILE`"
		echo "Sending mail to $TO (${SJ})..."
#		HOST=`dmc-smtp ${TO}`
		msmtp "--user=${USER}" "--from=${MAIL}" $TO < ${TMP}
		return $?
	elif [ "`echo \"${SEND}\" | grep '|'`" ]; then
		echo "=> cat $1 ${SEND}"
		# TODO: setup environment for $TO $SUBJECT ...
		eval cat ${TMP} ${SEND}
	else
		echo "SEND method '${SEND}' not supported"
	fi
	[ -e "${FILE}.d" ] && rm -f $TMP
	return 0
}

pull_mails () {
	chkcfg
	echo "Pulling mails from account '${NAME}'"
	# This is pop3-only
	i=1
	while [ ! "$LIMIT" = "$i" ] ; do
		dmc -c cat $i > ~/.dmc/box/${NAME}/in/$i.eml 2> ~/.dmc/tmp/${NAME}.tmp
		if [ -n "`cat ~/.dmc/tmp/${NAME}.tmp | grep 'cat 0'`" ]; then
			rm -f ~/.dmc/box/${NAME}/in/$i.eml
			echo "EOF $i"
			cat ~/.dmc/tmp/${NAME}.tmp
			break
		else
			size=`du -hs ~/.dmc/box/${NAME}/in/$i.eml | awk '{print \$1}'`
			echo "got $i $size $(cat ~/.dmc/tmp/${NAME}.tmp)"
		fi
		i=$(($i+1))
	done
}

ign () { : ; }

case "$1" in
"start")
	start_account_daemons
	;;
"stop")
	cd ~/.dmc/tmp
	for a in *.input ; do
		[ "$a" = "*.input" ] && break # XXX ugly hack
		file=`echo $a|cut -d '.' -f 1`
		echo Stopping $file daemon
		echo exit > $a &
		sleep 1
		rm -f ~/.dmc/tmp/$a
	done
	rm -f ~/.dmc/tmp/* 2> /dev/null
	pkill cat
	trap ign TERM
	pkill -TERM dmc
	;;
"push")
	for a in ~/.dmc/acc/* ; do
		. $a
		[ ! -d ~/.dmc/box/${NAME}/out ] && continue
		for b in ~/.dmc/box/${NAME}/out/* ; do
			[ ! -f "$b" ] && continue
			send_message "$b"
			if [ $? = 0 ]; then
				echo "Mail sent. local copy moved to ~/.dmc/box/${NAME}/sent"
				mkdir -p ~/.dmc/box/${NAME}/sent
				mv $b ~/.dmc/box/${NAME}/sent
				[ -e "${b}.d" ] && mv ${b}.d ~/.dmc/box/${NAME}/sent
			else
				echo "There was an error sending the mail"
			fi
		done
	done
	;;
"pull")
	for a in ~/.dmc/acc/* ; do
		. $a
		mkdir -p ~/.dmc/box/${NAME}/in
		pull_mails
	done
	;;
"-e"|"--edit")
	if [ -n "$2" ]; then
		[ -z "`cat ~/.dmc/acc/$2`" ] && \
			print_account_template "$2" > ~/.dmc/acc/$2
		${EDITOR} ~/.dmc/acc/$2
		if [ -z "`cat ~/.dmc/acc/$2`" ]; then
			rm -f ~/.dmc/acc/$2
		else
			echo "The '$2' account is now the default"
			ln -fs ~/.dmc/acc/$2 ~/.dmc/acc.default
		fi
	else
		ls ~/.dmc/acc | cat
	fi
	;;
"-c"|"--cmd")
	chkcfg
	if [ -z "$2" ]; then
		while [ ! "$A" = "exit" ] ; do
			printf "> "
			read A
			dmc_cmd "$A"
		done
	else
		shift
		dmc_cmd "$@"
	fi
	;;
"-s"|"--send")
	shift
	chkcfg
	if [ -z "$*" ]; then
		echo "Usage: dmc -s mail1 mail2 ..."
		exit 1
	fi
	for a in $* ; do
		send_message $a
	done
	;;
"-m"|"--mail")
	TO="$2"
	SUBJECT="$3"
	edit_message
	;;
"-A"|"--add-attachment")
	while [ -n "$2" ] ; do
		add_attachment $2
		shift
	done
	;;
"-a"|"--addr")
	if [ -n "$2" ]; then
		while [ -n "$2" ] ; do
			grep -e "$2" ~/.dmc/addrbook
			shift
		done
	else
		[ ! -f ~/.dmc/addrbook ] && touch ~/.dmc/addrbook
		${EDITOR} ~/.dmc/addrbook
	fi
	;;
"-l"|"--list")
	cd ~/.dmc/box/${NAME}/in
	if [ -n "$2" ]; then
		if [ -f "$2.eml" ]; then
			cat $2.eml
			exit 0
		fi
		cd $2
	fi
	for a in `ls -rt *.eml` ; do
		printf "$a:\t\x1b[32m"
		dmc-filter Subject < $a | head -n 1
		printf "\x1b[0m  \t- "
		dmc-filter To < $a | head -n 1
		printf "  \t- "
		dmc-filter Date < $a | head -n 1
	done
	;;
"-v"|"--version")
	echo "dmc v${VERSION}"
	;;
"--help"|"-h")
	echo ${HELP}
	echo " -e account     edit account information"
	echo " -a name        show addressbook email for contact"
	echo " -A file        add attachment to mail"
	echo " -c cmd         run command for \$DMC_ACCOUNT or acc.default daemon"
	echo " -m addr subj   create mail with default account"
	echo " -s file        send email"
	echo " -l [mail/fold] list mails in folder or show mail"
	echo " -v             show version"
	echo " -h             show this help message"
	echo " start          start mail daemons"
	echo " stop           stop them all"
	echo " push           send mails in box/*/out"
	echo " pull           update box/*/in folders"
	;;
*)
	echo "${HELP}"
	;;
esac

exit 0
