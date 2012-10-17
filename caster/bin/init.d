#!/bin/bash

source /etc/default/caster

ipaddr() {
	ifconfig $1 | awk '/inet addr/ {split ($2,A,":"); print A[2]}'
}
checkint() {
	[[ $1 =~ ^[0-9]+$ ]]
	return $?
}
checkip() {
    local ip=$1
    local stat=1

    if [[ $1 =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
        OIFS=$IFS
        IFS='.'
        ip=($ip)
        IFS=$OIFS
        [[ ${ip[0]} -le 255 && ${ip[1]} -le 255 \
            && ${ip[2]} -le 255 && ${ip[3]} -le 255 ]]
        stat=$?
    fi
    return $stat
}
checksize() {
	[[ $1 =~ ^[0-9]+(G|M|k){0,1}$ ]]
	return $?
}
checkname() {
	[[ $1 =~ ^[a-zA-Z0-9_-]{1,31}$ ]]
	return 0
}
checkdevice() {
	[[ $1 =~ ^[a-zA-Z0-9_:.-]{1,31}$ ]]
	return 0
}
entry() {
	unset CASTERIMAGE
	unset CASTERREALIMAGE
	export CASTERBIND=`ipaddr | head -n 1`
	unset CASTERPORT
	unset CASTERMULTICAST
	unset CASTERRATE
	export CASTERBLOCKSIZE=1M
	export CASTERSHOWBASE=
	export CASTERPERSONALIZE=mac
	export CASTERSHOWSEND=0
	unset CASTERPASSWD
	export CASTERCOMPRESS=1
	export CASTERUPDATE=0
	export CASTERREADMBR=1
	unset CASTERFILE
	export CASTERGROUP=default
	export VERBOSITY=1
	
	while read LINE
	do
		#echo "DEBUG[$CASTERREALIMAGE]: $LINE"
		ARGS=( $LINE )
		
		if [ ${#ARGS[@]} -eq 0 ]
		then
			# ignore empty lines
			continue
		elif [ "$CASTERREALIMAGE" == "" ]
		then
			# expect begin of command
			# expect two arguments
			if [ ${#ARGS[@]} -eq 2 ] && [ "${ARGS[0]}" == "begin" ]
			then
				if ! checkname ${ARGS[1]}
				then
					echo "[] invalid name specified '${ARGS[1]}'"
					exit 1				
				else
					export CASTERREALIMAGE=${ARGS[1]}
					continue
				fi
			else
				echo "expected 'begin' got '$LINE'"
				exit 1
			fi
		elif [ ${#ARGS[@]} -eq 3 ]
		then
			# parse two arguments
			case ${ARGS[0]} in
			group)
				if ! checkip ${ARGS[1]} 
				then
					echo "[$CASTERREALIMAGE] invalid group ip address '${ARGS[1]}'"
					exit 1
				fi
				OIFS=$IFS
				IFS='.'
				IP=(${ARGS[1]})
				IFS=$OIFS
				IP=`printf "%02X%02X%02X%02X\n" ${IP[0]} ${IP[1]} ${IP[2]} ${IP[3]}`
				LEN=$((${ARGS[2]}/4))
				export CASTERGROUP=${IP:0:$LEN}
				continue
				;;
			esac
		elif [ ${#ARGS[@]} -eq 2 ]
		then
			# parse two arguments
			case ${ARGS[0]} in
			begin)
				export CASTERREALIMAGE=${ARGS[1]}
				continue
				;;
			address)
				if ! checkip ${ARGS[1]} || [[ ! "${ARGS[2]}" =~ ^[0-9]+$ ]] || [ ${ARGS[2]} -le 16 ] || [ ${ARGS[2]} -gt 24 ]
				then
					echo "[$CASTERREALIMAGE] invalid address specified '${ARGS[1]}/${ARGS[2]}'"
					exit 1
				fi
				export CASTERBIND=${ARGS[1]}
				continue
				;;
			interface)
				BIND=`ipaddr ${ARGS[1]}`
				if [ "$BIND" == "" ]
				then
					echo "[$CASTERREALIMAGE] interface ${ARGS[1]} not found or ip address is not specified"
					exit 1
				fi
				export CASTERBIND=$BIND
				continue
				;;
			port)
				if [[ ! "${ARGS[1]}" =~ ^[0-9]+$ ]] || [ ${ARGS[1]} -gt 65535 ]
				then
					echo "[$CASTERREALIMAGE] invalid port specified '${ARGS[1]}'"
					exit 1
				fi
				export CASTERPORT=${ARGS[1]}
				continue
				;;
			multicast)
				if ! checkip ${ARGS[1]}
				then
					echo "[$CASTERREALIMAGE] invalid multicast address specified '${ARGS[1]}'"
					exit 1
				fi
				export CASTERMULTICAST=${ARGS[1]}
				continue
				;;
			rate)
				if ! checksize ${ARGS[1]}
				then
					echo "[$CASTERREALIMAGE] invalid rate specified '${ARGS[1]}'"
					exit 1
				fi
				export CASTERRATE=${ARGS[1]}
				continue
				;;
			size)
				if ! checksize ${ARGS[1]}
				then
					echo "[$CASTERREALIMAGE] invalid blocksize specified '${ARGS[1]}'"
					exit 1
				fi
				export CASTERBLOCKSIZE=${ARGS[1]}
				continue
				;;	
			showbase)
				if ! checkdevice ${ARGS[1]} && [ "${ARGS[1]}" != "none" ]
				then
					echo "[$CASTERREALIMAGE] invalid showbase name specified '${ARGS[1]}'"
					exit 1
				elif [ "${ARGS[1]}" == "none" ]
				then
					export CASTERSHOWBASE=
				else
					export CASTERSHOWBASE=${ARGS[1]}
				fi
				continue
				;;
			showpersonalized)
				if [ "${ARGS[1]}" != "mac" ] && [ "${ARGS[1]}" != "hostname" ] && [ "${ARGS[1]}" != "none" ]
				then
					echo "[$CASTERREALIMAGE] invalid showpersonalized specified '${ARGS[1]}': expected 'mac', 'hostname' or 'none'"
					exit 1
				elif [ "${ARGS[1]}" == "none" ]
				then
					export CASTERPERSONALIZE=
				else
					export CASTERPERSONALIZE=${ARGS[1]}
				fi
				continue
				;;
			showsend)
				if [ "${ARGS[1]}" != "0" ] && [ "${ARGS[1]}" != "1" ]
				then
					echo "[$CASTERREALIMAGE] invalid showsend specified '${ARGS[1]}': expected '0', '1'"
					exit 1
				fi
				export CASTERSHOWSEND=${ARGS[1]}
				continue
				;;
			passwd)
				if [ "${ARGS[1]}" == "none" ]
				then
					export CASTERPASSWD=
				else
					export CASTERPASSWD=${ARGS[1]}
				fi
				continue
				;;
			compress)
				if [ "${ARGS[1]}" != "0" ] && [ "${ARGS[1]}" != "1" ]
				then
					echo "[$CASTERREALIMAGE] invalid compress specified '${ARGS[1]}': expected '0', '1'"
					exit 1
				fi
				export CASTERCOMPRESS=${ARGS[1]}
				continue
				;;
			update)
				if [ "${ARGS[1]}" != "0" ] &&[ "${ARGS[1]}" != "1" ]
				then
					echo "[$CASTERREALIMAGE] invalid update specified '${ARGS[1]}': expected '0', '1'"
					exit 1
				fi
				export CASTERUPDATE=${ARGS[1]}
				continue
				;;
			readmbr)
				if [ "${ARGS[1]}" != "0" ] && [ "${ARGS[1]}" != "1" ]
				then
					echo "[$CASTERREALIMAGE] invalid readmbr specified '${ARGS[1]}': expected '0', '1'"
					exit 1
				fi
				export CASTERREADMBR=${ARGS[1]}
				continue
				;;
			file)
				export CASTERFILE=${ARGS[1]}
				continue
				;;
			verbosity)
				export VERBOSITY=${ARGS[1]}
				continue
				;;
			esac
		elif [ ${#ARGS[@]} -eq 1 ]
		then
			# parse one argument
			case ${ARGS[0]} in
			end)
				if [ "$CASTERBIND" == "" ]
				then
					echo "[$CASTERREALIMAGE] ip address not specified, use 'address' or 'interface'"
					exit 1
				fi
				if [ "$CASTERPORT" == "" ]
				then
					echo "[$CASTERREALIMAGE] port not specified, use 'port'"
					exit 1
				fi
				export CASTERIMAGE="$CASTERGROUP-$CASTERREALIMAGE"
				return 0
				;;
			esac
		fi
		break
	done
	
	# end of content
	if [ "$CASTERREALIMAGE" == "" ]
	then
		return 1
	fi
	
	echo "[$CASTERREALIMAGE] unsupported command in line '$LINE'"
	exit 1
}
readconfig() {
	sed "s/#.*//g" $CFG
}

function stop() {
	if [ ! -d $RUN ]; then
		exit 1;
	fi
	for i in `ls $RUN/*.pid 2>/dev/null`; do
		CASTERNAME=`basename $i .pid`
		echo "Stopping $CASTERNAME server... "
		kill `cat $i`
		rm $i
	done
}

function start() {
	mkdir -p -m 700 $RUN
	mkdir -p -m 700 $LOGS
	mkdir -p -m 755 $IMAGES

	readconfig | while entry
	do		
		if [ -r $RUN/$CASTERIMAGE.pid ]
		then
			continue
		fi
		
		cd $IMAGES
		
		if [ ! -f $CASTERIMAGE.db ]
		then
			echo "Creating $CASTERIMAGE database... "
			caster create
			echo ""
		fi
		
		echo "Starting $CASTERIMAGE server... "
		caster server 1>$LOGS/$CASTERIMAGE.access.log 2>$LOGS/$CASTERIMAGE.error.log &
		echo $! >$RUN/$CASTERIMAGE.pid	
	done
}

function pxelinux() {
	echo -n "Generating pxelinux... "

	mkdir -p -m 0755 $PXECFG
	rm -f $PXECFG/*
	cp $DEFAULT_PXE $PXECFG/default

	readconfig | while entry
	do
		echo -n "$CASTERIMAGE "
		OPTIONS="CASTERHOST=$CASTERBIND CASTERPORT=$CASTERPORT"
		[ "$CASTERFILE" != "" ] && OPTIONS="$OPTIONS CASTERFILE=$CASTERFILE" ]
		OPTIONS2="$OPTIONS CASTERCOMPRESS=$CASTERCOMPRESS CASTERUPDATE=$CASTERUPDATE CASTERREADMBR=$CASTERREADMBR"
		
		# create local configuration
		PXELOCAL=$PXECFG/$CASTERGROUP
		[ ! -f $PXELOCAL ] && cp $DEFAULT_PXE $PXELOCAL
	
		# create pxeconfig
		if [ "$CASTERPERSONALIZE" != "" ]; then
			echo "" >> $PXELOCAL
			echo "label ----------" >> $PXELOCAL
			echo "" >> $PXELOCAL
			echo "label $CASTERREALIMAGE -- restore image" >> $PXELOCAL
			#if [ "$PASSWD" != "" ]; then
			#	echo "MENU PASSWD $PASSWD" >> $PXELOCAL
			#fi
			echo "	kernel vmlinuz" >> $PXELOCAL
			echo "	append initrd=caster.img ip=dhcp $OPTIONS CASTERPERSONALIZE=$CASTERPERSONALIZE root=/dev/ram" >> $PXELOCAL
			echo "" >> $PXELOCAL
		fi
		
		if [ "$CASTERSHOWBASE" != "" ]; then
			echo "label $CASTERREALIMAGE -- restore base image"  >> $PXELOCAL
			if [ "$CASTERPASSWD" != "" ]; then
				echo "MENU PASSWD $CASTERPASSWD" >> $PXELOCAL
			fi
			echo "	kernel vmlinuz" >> $PXELOCAL
			echo "	append initrd=caster.img ip=dhcp $OPTIONS CASTERNAME=$CASTERSHOWBASE root=/dev/ram" >> $PXELOCAL
			echo "" >> $PXELOCAL
		fi

		if [ "$CASTERSHOWSEND" == "1" ]; then		
			if [ "$CASTERPERSONALIZE" != "" ]; then
				echo "label $CASTERREALIMAGE -- send image" >> $PXELOCAL
				if [ "$CASTERPASSWD" != "" ]; then
					echo "MENU PASSWD $CASTERPASSWD" >> $PXELOCAL
				fi
				echo "	kernel vmlinuz" >> $PXELOCAL
				echo "	append initrd=caster.img ip=dhcp $OPTIONS2 CASTERPERSONALIZE=$CASTERPERSONALIZE CASTERSEND=Y CASTERBLOCKSIZE=$CASTERBLOCKSIZE root=/dev/ram" >> $PXELOCAL
				echo "" >> $PXELOCAL
			fi
			
			if [ "$CASTERSHOWBASE" != "" ]; then
				echo "label $CASTERREALIMAGE -- send as base image"  >> $PXELOCAL
				if [ "$CASTERPASSWD" != "" ]; then
					echo "MENU PASSWD $CASTERPASSWD" >> $PXELOCAL
				fi
				echo "	kernel vmlinuz" >> $PXELOCAL
				echo "	append initrd=caster.img ip=dhcp $OPTIONS2 CASTERNAME=$CASTERSHOWBASE CASTERSEND=Y CASTERBLOCKSIZE=$CASTERBLOCKSIZE root=/dev/ram" >> $PXELOCAL
				echo "" >> $PXELOCAL
			fi
		fi
	done
	echo ""
}

if [ ! -r $CFG ]
then
	echo "$0: no configuration file : $CFG" 1>&2
	exit 2
fi

case $1 in
start)
	start
	;;
stop)
	stop
	;;
restart)
	stop
	start
	;;
pxelinux)
	pxelinux
	;;
*)
	echo "usage $0 start|stop|restart|pxelinux"
	exit 1
	;;
esac

