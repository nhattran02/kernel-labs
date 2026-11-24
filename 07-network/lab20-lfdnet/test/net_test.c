// SPDX-License-Identifier: GPL-2.0
/*
 * net_test - send raw Ethernet frames lfd0 -> lfd1 with AF_PACKET and query
 * the driver with the ethtool ioctl (Lab 20).
 */
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/sockios.h>
#include "lfd_test.h"

#define ETH_P_LFD 0x88B5	/* IEEE 802 local experimental EtherType 1 */
#define NFRAMES 2000

static int ifidx(const char *name) { return if_nametoindex(name); }

static int mac_of(const char *ifn, unsigned char mac[6])
{
	struct ifreq r = { 0 };
	int s = socket(AF_INET, SOCK_DGRAM, 0), ret;

	strncpy(r.ifr_name, ifn, IFNAMSIZ - 1);
	ret = ioctl(s, SIOCGIFHWADDR, &r);
	memcpy(mac, r.ifr_hwaddr.sa_data, 6);
	close(s);
	return ret;
}

static int ethtool(const char *ifn, void *cmd)
{
	struct ifreq r = { 0 };
	int s = socket(AF_INET, SOCK_DGRAM, 0), ret;

	strncpy(r.ifr_name, ifn, IFNAMSIZ - 1);
	r.ifr_data = cmd;
	ret = ioctl(s, SIOCETHTOOL, &r);
	close(s);
	return ret;
}

static int rx_count, rx_bad;
static volatile int rx_stop;

static void *receiver(void *arg)
{
	int s = *(int *)arg, expect = 0;
	unsigned char buf[2048];

	while (!rx_stop) {
		struct pollfd p = { .fd = s, .events = POLLIN };

		if (poll(&p, 1, 200) <= 0)
			continue;
		if (recv(s, buf, sizeof(buf), 0) < ETH_HLEN + 4)
			continue;
		if (ntohl(*(uint32_t *)(buf + ETH_HLEN)) != (uint32_t)expect)
			rx_bad++;
		expect++;
		rx_count++;
	}
	return NULL;
}

int main(void)
{
	struct ethtool_drvinfo di = { .cmd = ETHTOOL_GDRVINFO };
	struct ethtool_value link = { .cmd = ETHTOOL_GLINK };
	struct { struct ethtool_gstrings h; char d[3][ETH_GSTRING_LEN]; } gs;
	struct { struct ethtool_stats h; uint64_t v[3]; } st;
	struct sockaddr_ll sa = { 0 };
	unsigned char mac0[6], mac1[6], frame[ETH_HLEN + 100];
	pthread_t t;
	int tx, rx, i, sent = 0;

	CHECK(ethtool("lfd0", &di) == 0 && !strcmp(di.driver, "lfdnet"),
	      "ETHTOOL_GDRVINFO: driver \"%s\"", di.driver);
	CHECK(ethtool("lfd0", &link) == 0 && link.data == 1, "ETHTOOL_GLINK: link up");
	CHECK(mac_of("lfd0", mac0) == 0 && mac_of("lfd1", mac1) == 0, "read both MAC addresses");
	CHECK((mac0[0] & 3) == 2, "random MAC is locally administered, unicast");

	rx = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_LFD));
	sa.sll_family = AF_PACKET;
	sa.sll_protocol = htons(ETH_P_LFD);
	sa.sll_ifindex = ifidx("lfd1");
	CHECK(rx >= 0 && bind(rx, (struct sockaddr *)&sa, sizeof(sa)) == 0, "AF_PACKET receiver on lfd1");
	tx = socket(AF_PACKET, SOCK_RAW, 0);
	sa.sll_ifindex = ifidx("lfd0");
	sa.sll_halen = 6;
	memcpy(sa.sll_addr, mac1, 6);
	CHECK(tx >= 0, "AF_PACKET sender on lfd0");

	pthread_create(&t, NULL, receiver, &rx);
	memcpy(frame, mac1, 6);
	memcpy(frame + 6, mac0, 6);
	*(uint16_t *)(frame + 12) = htons(ETH_P_LFD);
	for (i = 0; i < NFRAMES; i++) {
		*(uint32_t *)(frame + ETH_HLEN) = htonl(i);
		while (sendto(tx, frame, sizeof(frame), 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
			if (errno != ENOBUFS)
				goto out;
			usleep(1000);		/* qdisc full: back off */
		}
		sent++;
	}
out:
	for (i = 0; i < 50 && rx_count < sent; i++)
		usleep(100000);
	rx_stop = 1;
	pthread_join(t, NULL);
	CHECK(sent == NFRAMES, "sent %d frames", sent);
	CHECK(rx_count == NFRAMES, "received %d of %d frames", rx_count, NFRAMES);
	CHECK(rx_bad == 0, "all in order (%d out of sequence)", rx_bad);

	gs.h.cmd = ETHTOOL_GSTRINGS;
	gs.h.string_set = ETH_SS_STATS;
	gs.h.len = 3;
	CHECK(ethtool("lfd1", &gs) == 0 && !strcmp(gs.d[1], "napi_polls"),
	      "ETHTOOL_GSTRINGS: \"%s\", \"%s\", \"%s\"", gs.d[0], gs.d[1], gs.d[2]);
	st.h.cmd = ETHTOOL_GSTATS;
	st.h.n_stats = 3;
	CHECK(ethtool("lfd1", &st) == 0 && st.v[1] > 0, "ETHTOOL_GSTATS: ring_full %llu, polls %llu, budget exhausted %llu",
	      (unsigned long long)st.v[0], (unsigned long long)st.v[1], (unsigned long long)st.v[2]);
	close(tx);
	close(rx);
	return lfd_summary();
}
