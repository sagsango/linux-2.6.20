
LINUX 2.6.20 NETWORK SUBSYSTEM
IDE STYLE STUDY NOTES
=====================

GOAL
----
Understand the complete networking stack from userspace socket()
to the NIC driver.

==================================================================
HIGH LEVEL FLOW
==================================================================

Userspace
    |
    | socket(), send(), recv()
    v
net/socket.c
    |
    v
Protocol Family (AF_INET, AF_UNIX, ...)
    |
    v
IPv4 / IPv6
    |
    v
Routing
    |
    v
Neighbour (ARP)
    |
    v
net/core/dev.c
    |
    v
drivers/net/
    |
    v
NIC DMA
    |
    v
Ethernet Wire

Receive Path

Wire
 |
NIC Interrupt
 |
drivers/net
 |
netif_rx()
 |
net/core
 |
IPv4
 |
TCP/UDP
 |
Socket
 |
recv()

==================================================================
MASTER READING ORDER
==================================================================

Phase 0
-------
Makefile
Kconfig

Phase 1
-------
socket.c
compat.c

Phase 2 (MOST IMPORTANT)
------------------------
core/

Read:

sock.c
dev.c
skbuff.c
datagram.c
dst.c
flow.c
neighbour.c
rtnetlink.c
filter.c
ethtool.c

Learn:

struct sock
struct socket
struct sk_buff
struct net_device
struct packet_type
NAPI

Phase 3
-------
ipv4/

Suggested order:

af_inet.c
route.c
ip_input.c
ip_output.c
tcp.c
tcp_input.c
tcp_output.c
udp.c
icmp.c
raw.c

Phase 4
-------
ipv6/

Phase 5
-------
ethernet/
802/
8021q/
llc/

Phase 6
-------
packet/

(AF_PACKET)

Phase 7
-------
netfilter/

Hooks
NAT
iptables
conntrack

Phase 8
-------
sched/

Traffic control
Queue disciplines

Phase 9
-------
unix/

AF_UNIX sockets

Phase 10
--------
netlink/

Kernel <-> Userspace

Phase 11
--------
ieee80211/

Wireless protocol

Phase 12
--------
dccp/
sctp/
tipc/
atm/
bluetooth/
irda/
rxrpc/

Phase 13
--------
key/
netlabel/
xfrm/

Security

Phase 14
--------
Historical protocols

appletalk/
ipx/
decnet/
econet/
ax25/
lapb/
netrom/
rose/
x25/

==================================================================
DIRECTORY PURPOSE
==================================================================

core/
    Networking infrastructure.

ipv4/
    IPv4 implementation.

ipv6/
    IPv6 implementation.

ethernet/
    Ethernet protocol helpers.

802/
    IEEE 802 support.

8021q/
    VLAN support.

llc/
    Logical Link Control.

packet/
    Raw packet sockets (tcpdump).

netfilter/
    Firewall/NAT.

sched/
    QoS / Traffic shaping.

unix/
    UNIX domain sockets.

netlink/
    Kernel-user communication.

ieee80211/
    Wireless protocol support.

dccp/
    Datagram Congestion Control Protocol.

sctp/
    SCTP transport protocol.

tipc/
    Transparent Inter Process Communication.

atm/
    ATM networking.

bluetooth/
    Bluetooth stack.

irda/
    Infrared networking.

rxrpc/
    RXRPC protocol.

key/
    Kernel key management.

netlabel/
    Security labels.

xfrm/
    IPSec transformation framework.

==================================================================
PACKET TRANSMIT FLOW
==================================================================

Application
 |
send()
 |
socket layer
 |
TCP / UDP
 |
IPv4
 |
Route lookup
 |
Neighbour (ARP)
 |
dev_queue_xmit()
 |
NIC driver
 |
DMA descriptors
 |
Ethernet controller
 |
Wire

==================================================================
PACKET RECEIVE FLOW
==================================================================

Wire
 |
NIC
 |
IRQ
 |
Driver
 |
DMA completed
 |
Allocate skb
 |
netif_rx()
 |
IP
 |
TCP/UDP
 |
Socket
 |
recv()

==================================================================
RELATIONSHIP
==================================================================

Userspace
   |
socket.c
   |
core/
   |
ipv4/ ipv6/ unix/
   |
ethernet/
   |
netfilter/
   |
sched/
   |
drivers/net/
   |
Hardware

==================================================================
MOST IMPORTANT FILES
==================================================================

net/socket.c
net/core/dev.c
net/core/skbuff.c
net/core/sock.c
net/ipv4/af_inet.c
net/ipv4/ip_input.c
net/ipv4/ip_output.c
net/ipv4/tcp.c
net/ipv4/udp.c
net/netfilter/
drivers/net/8139too.c
drivers/net/e100.c

==================================================================
TIME INVESTMENT
==================================================================

core          30%
ipv4          20%
socket         8%
netfilter      8%
drivers/net   15%
ipv6           5%
ethernet       5%
packet         3%
unix           3%
netlink        3%
others        10%

