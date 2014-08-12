/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: project-conf.h,v 1.1 2010/10/21 18:23:44 joxe Exp $
 */

#ifndef __PROJECT_CONF_H__
#define __PROJECT_CONF_H__

/* The cc2420 transmission power (min:0, max: 31) */
#define RF_POWER                31

/* The cc2420 RSSI threshold (-32 is the reset value for -77 dBm) */
#define RSSI_THR        (-32-14)

#define UP_ONLY 1
#define ALL_NODES_ADDRESSABLE 0

#undef RPL_CONF_MOP
#if UP_ONLY
#define RPL_CONF_MOP RPL_MOP_NO_DOWNWARD_ROUTES
#else
#define RPL_CONF_MOP RPL_MOP_STORING_NO_MULTICAST
#endif

/* RPL and neighborhood information */

#define RPL_CONF_INIT_LINK_METRIC 6 /* default 5 */

/* Reject parents that have a higher link metric than the following. */
#define MAX_LINK_METRIC     10

#undef NEIGHBOR_CONF_MAX_NEIGHBORS
#undef UIP_CONF_DS6_NBR_NBU
#undef RPL_CONF_MAX_PARENTS_PER_DAG

#define NEIGHBOR_CONF_MAX_NEIGHBORS 16
#define UIP_CONF_DS6_NBR_NBU  16
#define RPL_CONF_MAX_PARENTS_PER_DAG 16

#undef UIP_CONF_DS6_ROUTE_NBU
#define UIP_CONF_DS6_ROUTE_NBU  70

/* Other system parameters */

#undef WITH_PHASE_OPTIMIZATION
#define WITH_PHASE_OPTIMIZATION CMD_PHASE_LOCK

#undef CC2420_CONF_SFD_TIMESTAMPS
#define CC2420_CONF_SFD_TIMESTAMPS 0

#undef SICSLOWPAN_CONF_MAX_MAC_TRANSMISSIONS
#define SICSLOWPAN_CONF_MAX_MAC_TRANSMISSIONS   5

#define MIN_DIO_RECEIVED 3


#undef RPL_CONF_STATS
#define RPL_CONF_STATS 0

#define UIP_CONF_DS6_ADDR_NBU 1

//#define RPL_CONF_DIO_INTERVAL_MIN 12  /* default 12 */
//#define RPL_CONF_DIO_INTERVAL_DOUBLINGS 8 /* default 8 */
#define RPL_CONF_DIO_INTERVAL_MIN 12  /* default 12 */
#define RPL_CONF_DIO_INTERVAL_DOUBLINGS 8 /* default 8 */
#define RPL_CONF_DIO_REDUNDANCY   200  /* default 10 */

#define RPL_CONF_MAX_INSTANCES    1 /* default 1 */
#define RPL_CONF_MAX_DAG_PER_INSTANCE 1 /* default 2 */

/* Other system parameters */

#undef UIP_CONF_BUFFER_SIZE
#define UIP_CONF_BUFFER_SIZE    160

#undef UIP_CONF_UDP_CONNS
#define UIP_CONF_UDP_CONNS       2

#undef UIP_CONF_FWCACHE_SIZE
#define UIP_CONF_FWCACHE_SIZE    4

#undef UIP_CONF_TCP
#define UIP_CONF_TCP                    0

#undef UIP_CONF_UDP_CHECKSUMS
#define UIP_CONF_UDP_CHECKSUMS   0

#undef SICSLOWPAN_CONF_FRAG
#define SICSLOWPAN_CONF_FRAG 0

#undef DCOSYNCH_CONF_ENABLED
#define DCOSYNCH_CONF_ENABLED 0

#undef RF_CHANNEL
#define RF_CHANNEL              15

//#define CC2420_TXPOWER_MAX  31
//#define CC2420_TXPOWER_MIN   0
#define RF_POWER                31
//#define RF_POWER                7

/* 32-bit rtimer */
#define RTIMER_CONF_SECOND (4096UL*8)
typedef uint32_t rtimer_clock_t;
#define RTIMER_CLOCK_LT(a,b)     ((int32_t)(((rtimer_clock_t)a)-((rtimer_clock_t)b)) < 0)

#define CONTIKIMAC_CONF_CYCLE_TIME (CMD_CYCLE_TIME * RTIMER_ARCH_SECOND / 1000)


#undef CSMA_CONF_MAX_NEIGHBOR_QUEUES
#define CSMA_CONF_MAX_NEIGHBOR_QUEUES 4

#undef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 4

#endif /* __PROJECT_CONF_H__ */
