#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

// Bitmap for bound ports.
static uint64 bindmap[65536 / sizeof(uint64)];

// Array of sockets.
static struct socket sockets[MAX_SOCKETS];

// Locks.
static struct spinlock bindmap_lock;
static struct spinlock sockets_lock;
static struct spinlock netlock;


void
netinit(void)
{
  initlock(&bindmap_lock, "bindmap_lock");
  initlock(&sockets_lock, "sockets_lock");
  initlock(&netlock, "netlock");
  for(int i = 0; i < MAX_SOCKETS; i++){
    initlock(&sockets[i].rx_queue->lock, "rx_queue_lock"); 
  }
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //
  int port;
  argint(0, &port);

  if (port < 0 || port >= MAX_PORTS) {
    printf("bind: invalid port %d\n", port);
    return -1;
  }

  acquire(&bindmap_lock);
  if (bindmap[port / sizeof(uint64)] & (1U << (port % sizeof(uint64)))) {
    release(&bindmap_lock);
    printf("bind: port %d already bound\n", port);
    return -1;
  }

  bindmap[port / sizeof(uint64)] |= (1U << (port % sizeof(uint64)));
  release(&bindmap_lock);

  // Allocate a socket for the port.
  // For simplicity, we assume the socket's protocol type is UDP.
  allocsock(IPPROTO_UDP, port, local_ip);

  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  int port;
  argint(0, &port);

  if (port < 0 || port >= MAX_PORTS) {
    printf("unbind: invalid port %d\n", port);
    return -1;
  }

  acquire(&bindmap_lock);
  if (!(bindmap[port / sizeof(uint64)] & (1U << (port % sizeof(uint64))))) {
    release(&bindmap_lock);
    printf("unbind: port %d not bound\n", port);
    return -1;
  }

  bindmap[port / sizeof(uint64)] &= ~(1U << (port % sizeof(uint64)));
  release(&bindmap_lock);

  // Free the socket associated with the port.
  freesock(port);

  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  //
  return -1;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

//
// receive an IP packet.
void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //
  uint16 dport;
  uint8 protocol;
  struct socket *sock;
  int sock_idx;

  if(len < sizeof(struct eth) + sizeof(struct ip) || len > PGSIZE){
    printf("ip_rx: invalid packet length %d\n", len);
    kfree(buf);
    return;
  }

  struct eth *ineth = (struct eth *) buf;
  struct ip *inip = (struct ip *)(ineth + 1);

  dport = ntohs(inip->ip_dst);
  protocol = inip->ip_p;

  if(protocol != IPPROTO_UDP){
    printf("ip_rx: Only UDP packets are supported\n");
    kfree(buf);
    return;
  }

  acquire(&bindmap_lock);
  if(!(bindmap[dport / sizeof(uint64)] & (1U << (dport % sizeof(uint64))))){
    release(&bindmap_lock);
    printf("ip_rx: No socket bound to port %d\n", dport);
    kfree(buf);
    return;
  }
  release(&bindmap_lock);
  
  // Find the socket for the destination port.
  for(sock_idx = 0; sock_idx < MAX_SOCKETS; sock_idx++){
    sock = &sockets[sock_idx];
    if(sock->type == IPPROTO_UDP && sock->local_port == dport){
      break;
    }
  }
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}

void *
allocsock(uint8 type, uint16 local_port, uint32 local_ip)
{
  struct socket *sock;
  int sock_idx;

  acquire(&sockets_lock);
  for(sock_idx = 0; sock_idx < MAX_SOCKETS; sock_idx++){
    sock = &sockets[sock_idx];
    if(sock->type == 0){
      // socket is free.
      sock->type = type;
      sock->local_port = local_port;
      sock->local_ip = local_ip;
      sock->remote_port = 0;
      sock->remote_ip = 0;
      sock->rx_queue = 0;
      release(&sockets_lock);
      return sock;
    }
  }
  release(&sockets_lock);
  return 0; // No free socket found.
}

void
freesock(uint16 local_port)
{
  struct socket *sock;
  char *buf;
  int sock_idx, queue_idx;

  acquire(&sockets_lock);
  for(sock_idx = 0; sock_idx < MAX_SOCKETS; sock_idx++){
    sock = &sockets[sock_idx];
    if(sock->local_port == local_port){
      // Found the socket to free.
      sock->type = 0; // Mark the socket as free.
      sock->local_port = 0;
      sock->local_ip = 0;
      sock->remote_port = 0;
      sock->remote_ip = 0;
      if(sock->rx_queue) {
        // Free the receive queue if it exists.
        acquire(&sock->rx_queue->lock);

        // Free all packets in the receive queue.
        while(sock->rx_queue->count > 0){
          queue_idx = sock->rx_queue->head;
          buf = sock->rx_queue->queue[queue_idx];
          kfree(buf);
          sock->rx_queue->queue[queue_idx] = 0;
          sock->rx_queue->count--;
          queue_idx = (queue_idx + 1) % RX_QUEUE_SIZE;
        }

        // Reset the receive queue.
        sock->rx_queue->count = 0;
        sock->rx_queue->head = 0;
        sock->rx_queue->tail = 0;
        sock->rx_queue = 0;

        release(&sock->rx_queue->lock);
      }
      release(&sockets_lock);
      return;
    }
  }
  release(&sockets_lock);
}
