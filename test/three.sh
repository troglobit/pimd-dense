#!/bin/sh
# Three routers in a row, end devices on each end.  Every other
# device in a network namespace to circumvent one-address per
# subnet in Linux
#
# NS1         NS2              NS3              NS4              NS5
# [ED1:eth0]--[eth1:R1:eth2]---[eth3:R2:eth4]---[eth5:R3:eth6]---[eth0:ED2]
#       10.0.0.0/24     10.0.1.0/24      10.0.2.0/24      10.0.3.0/24

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

# Requires OSPF (bird) to build the unicast rpf tree
print "Check deps ..."
check_dep ethtool
check_dep tshark
check_dep bird

print "Creating world ..."
NS1="/tmp/$NM/NS1"
NS2="/tmp/$NM/NS2"
NS3="/tmp/$NM/NS3"
NS4="/tmp/$NM/NS4"
NS5="/tmp/$NM/NS5"
touch "$NS1" "$NS2" "$NS3" "$NS4" "$NS5"

echo "$NS1"  > "/tmp/$NM/mounts"
echo "$NS2" >> "/tmp/$NM/mounts"
echo "$NS3" >> "/tmp/$NM/mounts"
echo "$NS4" >> "/tmp/$NM/mounts"
echo "$NS5" >> "/tmp/$NM/mounts"

ip link add eth0 type veth peer eth1
ip link add eth2 type veth peer eth3
ip link add eth4 type veth peer eth5
ip link add eth6 type veth peer eth7

# Disabling UDP checksum offloading, frames are leaving kernel space
# on these VETH pairs.  (Silence noisy ethtool output)
for iface in eth0 eth1 eth2 eth3 eth4 eth5 eth6 eth7; do
    ethtool --offload "$iface" tx off >/dev/null
    ethtool --offload "$iface" rx off >/dev/null
done

unshare --net="$NS1" -- ip link set lo up
unshare --net="$NS2" -- ip link set lo up
unshare --net="$NS3" -- ip link set lo up
unshare --net="$NS4" -- ip link set lo up
unshare --net="$NS5" -- ip link set lo up

nsmove "$NS1" eth0
nsmove "$NS2" eth1 eth2
nsmove "$NS3" eth3 eth4
nsmove "$NS4" eth5 eth6
nsmove "$NS5" eth7

nsrename "$NS5" eth7 eth0

nsenter --net="$NS1" -- ip link set eth0 up
nsenter --net="$NS1" -- ip addr add 10.0.0.10/24 broadcast + dev eth0
nsenter --net="$NS1" -- ip route add default via 10.0.0.1

nsenter --net="$NS2" -- ip link set eth1 up
nsenter --net="$NS2" -- ip addr add 10.0.0.1/24 broadcast + dev eth1
nsenter --net="$NS2" -- ip link set eth2 up
nsenter --net="$NS2" -- ip addr add 10.0.1.1/24 broadcast + dev eth2

nsenter --net="$NS3" -- ip link set eth3 up
nsenter --net="$NS3" -- ip addr add 10.0.1.2/24 broadcast + dev eth3
nsenter --net="$NS3" -- ip link set eth4 up
nsenter --net="$NS3" -- ip addr add 10.0.2.1/24 broadcast + dev eth4

nsenter --net="$NS4" -- ip link set eth5 up
nsenter --net="$NS4" -- ip addr add 10.0.2.2/24 broadcast + dev eth5
nsenter --net="$NS4" -- ip link set eth6 up
nsenter --net="$NS4" -- ip addr add 10.0.3.1/24 broadcast + dev eth6

nsenter --net="$NS5" -- ip link set eth0 up
nsenter --net="$NS5" -- ip addr add 10.0.3.10/24 broadcast + dev eth0
nsenter --net="$NS5" -- ip route add default via 10.0.3.1

dprint "$NS1"
nsenter --net="$NS1" -- ip -br l
nsenter --net="$NS1" -- ip -br a
dprint "$NS2"
nsenter --net="$NS2" -- ip -br l
nsenter --net="$NS2" -- ip -br a
dprint "$NS3"
nsenter --net="$NS3" -- ip -br l
nsenter --net="$NS3" -- ip -br a
dprint "$NS4"
nsenter --net="$NS4" -- ip -br l
nsenter --net="$NS4" -- ip -br a
dprint "$NS5"
nsenter --net="$NS5" -- ip -br l
nsenter --net="$NS5" -- ip -br a

print "Creating OSPF config ..."
cat <<EOF > "/tmp/$NM/bird.conf"
protocol device {
}
protocol direct {
        ipv4;
}
protocol kernel {
	ipv4 {
        	export all;
	};
        learn;
}
protocol ospf {
	ipv4 {
	        import all;
	};
        area 0 {
                interface "eth*" {
                        type broadcast;
			hello 1;
			wait  3;
			dead  5;
                };
        };
}
EOF
cat "/tmp/$NM/bird.conf"

print "Starting Bird OSPF ..."
nsenter --net="$NS2" -- bird -c "/tmp/$NM/bird.conf" -f -s "/tmp/$NM/r1-bird.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$NS3" -- bird -c "/tmp/$NM/bird.conf" -f -s "/tmp/$NM/r2-bird.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$NS4" -- bird -c "/tmp/$NM/bird.conf" -f -s "/tmp/$NM/r3-bird.sock" &
echo $! >> "/tmp/$NM/PIDs"
sleep 1

print "Starting pimd-dense ..."
nsenter --net="$NS2" -- ../src/pimdd -n -p "/tmp/$NM/r1.pid" -l debug -u "/tmp/$NM/r1.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$NS3" -- ../src/pimdd -n -p "/tmp/$NM/r2.pid" -l debug -u "/tmp/$NM/r2.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$NS4" -- ../src/pimdd -n -p "/tmp/$NM/r3.pid" -l debug -u "/tmp/$NM/r3.sock" &
echo $! >> "/tmp/$NM/PIDs"
sleep 1

# Wait for routers to peer
print "Waiting for OSPF routers to peer (10 sec) ..."
tenacious nsenter --net="$NS1" -- ping -qc 1 -W 1 10.0.3.10 >/dev/null
dprint "OK"

#dprint "OSPF State & Routing Table $NS2:"
#nsenter --net="$NS2" -- echo "show ospf state" | birdc -s "/tmp/$NM/r1-bird.sock"
#nsenter --net="$NS2" -- echo "show ospf int"   | birdc -s "/tmp/$NM/r1-bird.sock"
#nsenter --net="$NS2" -- echo "show ospf neigh" | birdc -s "/tmp/$NM/r1-bird.sock"
#nsenter --net="$NS2" -- ip route
#
#dprint "OSPF State & Routing Table $NS3:"
#nsenter --net="$NS4" -- echo "show ospf state" | birdc -s "/tmp/$NM/r2-bird.sock"
#nsenter --net="$NS4" -- echo "show ospf int"   | birdc -s "/tmp/$NM/r2-bird.sock"
#nsenter --net="$NS3" -- echo "show ospf neigh" | birdc -s "/tmp/$NM/r2-bird.sock"
#nsenter --net="$NS3" -- ip route
#
#dprint "OSPF State & Routing Table $NS4:"
#nsenter --net="$NS4" -- echo "show ospf state" | birdc -s "/tmp/$NM/r3-bird.sock"
#nsenter --net="$NS4" -- echo "show ospf int"   | birdc -s "/tmp/$NM/r3-bird.sock"
#nsenter --net="$NS4" -- echo "show ospf neigh" | birdc -s "/tmp/$NM/r3-bird.sock"
#nsenter --net="$NS4" -- ip route

print "Starting emitter ..."
nsenter --net="$NS5" -- ./mping -qr -d -i eth0 -t 5 -W 30 225.1.2.3 &
echo $! >> "/tmp/$NM/PIDs"
sleep 1

if ! nsenter --net="$NS1"  -- ./mping -s -d -i eth0 -t 5 -c 10 -w 15 225.1.2.3; then
    dprint "PIM Status $NS2"
    nsenter --net="$NS2" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show compat detail
    dprint "PIM Status $NS3"
    nsenter --net="$NS3" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show compat detail
    dprint "PIM Status $NS4"
    nsenter --net="$NS4" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show compat detail
    echo "Failed routing, expected at least 10 multicast ping replies"
    FAIL
fi

OK
