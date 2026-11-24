/* SPDX-License-Identifier: GPL-2.0 */
/* IPv6 stub header for VXLAN - provides fallback implementations */

#ifndef _NET_IPV6_STUBS_H
#define _NET_IPV6_STUBS_H

#include <linux/ipv6.h>

static inline bool ipv6_addr_equal(const struct in6_addr *a1, const struct in6_addr *a2)
{
	return ((a1->s6_addr32[0] ^ a2->s6_addr32[0]) |
		(a1->s6_addr32[1] ^ a2->s6_addr32[1]) |
		(a1->s6_addr32[2] ^ a2->s6_addr32[2]) |
		(a1->s6_addr32[3] ^ a2->s6_addr32[3])) == 0;
}

#endif /* _NET_IPV6_STUBS_H */