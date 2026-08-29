#!/bin/bash

# Network prefix used by both setup and health checks.
. "$(dirname "$0")"/etc/tunconfig

show_usage () {
    echo "Usage: $0 <start | stop | restart | check> [tunnum ...]"
    exit 1
}

start_tun () {
    local TUNNUM="$1" TUNDEV="tun$1"
    ip tuntap add mode tun user "${SUDO_USER}" name "${TUNDEV}"
    ip addr add "${TUN_IP_PREFIX}.${TUNNUM}.1/24" dev "${TUNDEV}"
    ip link set dev "${TUNDEV}" up
    ip route change "${TUN_IP_PREFIX}.${TUNNUM}.0/24" dev "${TUNDEV}" rto_min 10ms

    # Apply NAT (masquerading) only to traffic from CS144's network devices
    iptables -t nat -A PREROUTING -s ${TUN_IP_PREFIX}.${TUNNUM}.0/24 -j CONNMARK --set-mark ${TUNNUM}
    iptables -t nat -A POSTROUTING -j MASQUERADE -m connmark --mark ${TUNNUM}
    # Docker, WSL, and some CI images set the FORWARD policy to DROP. Permit
    # traffic originating from this lab subnet, plus replies returning to it.
    iptables -C FORWARD -s ${TUN_IP_PREFIX}.${TUNNUM}.0/24 -j ACCEPT 2>/dev/null || \
        iptables -A FORWARD -s ${TUN_IP_PREFIX}.${TUNNUM}.0/24 -j ACCEPT
    iptables -C FORWARD -d ${TUN_IP_PREFIX}.${TUNNUM}.0/24 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || \
        iptables -A FORWARD -d ${TUN_IP_PREFIX}.${TUNNUM}.0/24 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
    echo 1 > /proc/sys/net/ipv4/ip_forward
}

stop_tun () {
    local TUNDEV="tun$1"
    iptables -t nat -D PREROUTING -s ${TUN_IP_PREFIX}.${1}.0/24 -j CONNMARK --set-mark ${1}
    iptables -t nat -D POSTROUTING -j MASQUERADE -m connmark --mark ${1}
    iptables -C FORWARD -s ${TUN_IP_PREFIX}.${1}.0/24 -j ACCEPT 2>/dev/null && \
        iptables -D FORWARD -s ${TUN_IP_PREFIX}.${1}.0/24 -j ACCEPT
    iptables -C FORWARD -d ${TUN_IP_PREFIX}.${1}.0/24 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null && \
        iptables -D FORWARD -d ${TUN_IP_PREFIX}.${1}.0/24 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
    ip tuntap del mode tun name "$TUNDEV"
}

start_all () {
    while [ ! -z "$1" ]; do
        local INTF="$1"; shift
        start_tun "$INTF"
    done
}

stop_all () {
    while [ ! -z "$1" ]; do
        local INTF="$1"; shift
        stop_tun "$INTF"
    done
}

restart_all() {
    stop_all "$@"
    start_all "$@"
}

check_tun () {
    [ "$#" != 1 ] && { echo "bad params in check_tun"; exit 1; }
    local TUNDEV="tun${1}"
    # make sure tun is healthy: device is up, ip_forward is set, and iptables is configured
    ip link show ${TUNDEV} &>/dev/null || return 1
    [ "$(cat /proc/sys/net/ipv4/ip_forward)" = "1" ] || return 2
    iptables -C FORWARD -s ${TUN_IP_PREFIX}.${1}.0/24 -j ACCEPT 2>/dev/null || return 2
    iptables -C FORWARD -d ${TUN_IP_PREFIX}.${1}.0/24 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || return 2
}

check_sudo () {
    if [ "$SUDO_USER" = "root" ]; then
        echo "please execute this script as a regular user, not as root"
        exit 1
    fi
    if [ -z "$SUDO_USER" ]; then
        # if the user didn't call us with sudo, re-execute
        exec sudo $0 "$MODE" "$@"
    fi
}

# check arguments
if [ -z "$1" ] || ([ "$1" != "start" ] && [ "$1" != "stop" ] && [ "$1" != "restart" ] && [ "$1" != "check" ]); then
    show_usage
fi
MODE=$1; shift

# set default argument
if [ "$#" = "0" ]; then
    set -- 144 145
fi

# Health checks include firewall rules and therefore need the same privileges
# as start/stop operations.
check_sudo "$@"

# Like start, but exit successfully if everything is OK.
if [ "$MODE" = "check" ]; then
    declare -a INTFS
    MODE="start"
    while [ ! -z "$1" ]; do
        INTF="$1"; shift
        check_tun ${INTF}
        RET=$?
        if [ "$RET" = "0" ]; then
            continue
        fi

        if [ "$((RET > 1))" = "1" ]; then
            MODE="restart"
        fi
        INTFS+=($INTF)
    done

    # address only the interfaces that need it
    set -- "${INTFS[@]}"
    if [ "$#" = "0" ]; then
        exit 0
    fi
    echo -e "[$0] Bringing up tunnels ${INTFS[@]}:"
fi

# start, stop, or restart all intfs
eval "${MODE}_all" "$@"
