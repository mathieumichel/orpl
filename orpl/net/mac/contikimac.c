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
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         Implementation of the ContikiMAC power-saving radio duty cycling protocol
 * \author
 *         Adam Dunkels <adam@sics.se>
 *         Niclas Finne <nfi@sics.se>
 *         Joakim Eriksson <joakime@sics.se>
 */

#include "contiki-conf.h"
#include "dev/leds.h"
#include "dev/radio.h"
#include "dev/watchdog.h"
#include "lib/random.h"
#include "net/mac/contikimac.h"
#include "net/netstack.h"
#include "net/rime.h"
#include "sys/compower.h"
#include "sys/pt.h"
#include "sys/rtimer.h"
#include "anycast.h"
#include "net/neighbor-info.h"
#include "node-id.h"
#include "tools/rpl-tools.h"

#include <string.h>

/* TX/RX cycles are synchronized with neighbor wake periods */
#ifndef WITH_PHASE_OPTIMIZATION
#define WITH_PHASE_OPTIMIZATION      1
#endif
/* Two byte header added to allow recovery of padded short packets */
/* Wireshark will not understand such packets at present */
#ifdef CONTIKIMAC_CONF_WITH_CONTIKIMAC_HEADER
#define WITH_CONTIKIMAC_HEADER       CONTIKIMAC_CONF_WITH_CONTIKIMAC_HEADER
#else
#define WITH_CONTIKIMAC_HEADER       1
#endif
/* More aggressive radio sleeping when channel is busy with other traffic */
#ifndef WITH_FAST_SLEEP
#define WITH_FAST_SLEEP              1
#endif
/* Radio does CSMA and autobackoff */
#ifndef RDC_CONF_HARDWARE_CSMA
#define RDC_CONF_HARDWARE_CSMA       0
#endif
/* Radio returns TX_OK/TX_NOACK after autoack wait */
#ifndef RDC_CONF_HARDWARE_ACK
#define RDC_CONF_HARDWARE_ACK        0
#endif
/* MCU can sleep during radio off */
#ifndef RDC_CONF_MCU_SLEEP
#define RDC_CONF_MCU_SLEEP           0
#endif

#if NETSTACK_RDC_CHANNEL_CHECK_RATE >= 64
#undef WITH_PHASE_OPTIMIZATION
#define WITH_PHASE_OPTIMIZATION 0
#endif

#if WITH_CONTIKIMAC_HEADER
#define CONTIKIMAC_ID 0x00

struct hdr {
  uint8_t id;
  uint8_t len;
};
#endif /* WITH_CONTIKIMAC_HEADER */

/* CYCLE_TIME for channel cca checks, in rtimer ticks. */
#ifdef CONTIKIMAC_CONF_CYCLE_TIME
#define CYCLE_TIME (CONTIKIMAC_CONF_CYCLE_TIME)
#else
#define CYCLE_TIME (RTIMER_ARCH_SECOND / NETSTACK_RDC_CHANNEL_CHECK_RATE)
#endif

/* CHANNEL_CHECK_RATE is enforced to be a power of two.
 * If RTIMER_ARCH_SECOND is not also a power of two, there will be an inexact
 * number of channel checks per second due to the truncation of CYCLE_TIME.
 * This will degrade the effectiveness of phase optimization with neighbors that
 * do not have the same truncation error.
 * Define SYNC_CYCLE_STARTS to ensure an integral number of checks per second.
 */
#if RTIMER_ARCH_SECOND & (RTIMER_ARCH_SECOND - 1)
#define SYNC_CYCLE_STARTS                    1
#endif

/* Are we currently receiving a burst? */
static int we_are_receiving_burst = 0;
/* Has the receiver been awoken by a burst we're sending? */
static int is_receiver_awake = 0;

/* BURST_RECV_TIME is the maximum time a receiver waits for the
   next packet of a burst when FRAME_PENDING is set. */
#define INTER_PACKET_DEADLINE               CLOCK_SECOND / 32

/* ContikiMAC performs periodic channel checks. Each channel check
   consists of two or more CCA checks. CCA_COUNT_MAX is the number of
   CCAs to be done for each periodic channel check. The default is
   two.*/
#define CCA_COUNT_MAX                      2

/* Before starting a transmission, Contikimac checks the availability
   of the channel with CCA_COUNT_MAX_TX consecutive CCAs */
#define CCA_COUNT_MAX_TX                   2

/* CCA_CHECK_TIME is the time it takes to perform a CCA check. */
/* Note this may be zero. AVRs have 7612 ticks/sec, but block until cca is done */
#define CCA_CHECK_TIME                     RTIMER_ARCH_SECOND / 8192

/* CCA_SLEEP_TIME is the time between two successive CCA checks. */
/* Add 1 when rtimer ticks are coarse */
#if RTIMER_ARCH_SECOND > 8000
//#define CCA_SLEEP_TIME                     RTIMER_ARCH_SECOND / 2000
#define CCA_SLEEP_TIME                     RTIMER_ARCH_SECOND / 600
#else
#define CCA_SLEEP_TIME                     (RTIMER_ARCH_SECOND / 2000) + 1
#endif

/* CHECK_TIME is the total time it takes to perform CCA_COUNT_MAX
   CCAs. */
#define CHECK_TIME                         (CCA_COUNT_MAX * (CCA_CHECK_TIME + CCA_SLEEP_TIME))

/* CHECK_TIME_TX is the total time it takes to perform CCA_COUNT_MAX_TX
   CCAs. */
#define CHECK_TIME_TX                      (CCA_COUNT_MAX_TX * (CCA_CHECK_TIME + CCA_SLEEP_TIME))

/* LISTEN_TIME_AFTER_PACKET_DETECTED is the time that we keep checking
   for activity after a potential packet has been detected by a CCA
   check. */
#define LISTEN_TIME_AFTER_PACKET_DETECTED  RTIMER_ARCH_SECOND / 80

/* MAX_SILENCE_PERIODS is the maximum amount of periods (a period is
   CCA_CHECK_TIME + CCA_SLEEP_TIME) that we allow to be silent before
   we turn of the radio. */
#define MAX_SILENCE_PERIODS                5

/* MAX_NONACTIVITY_PERIODS is the maximum number of periods we allow
   the radio to be turned on without any packet being received, when
   WITH_FAST_SLEEP is enabled. */
#define MAX_NONACTIVITY_PERIODS            10



/* STROBE_TIME is the maximum amount of time a transmitted packet
   should be repeatedly transmitted as part of a transmission. */
#define STROBE_TIME                        (CYCLE_TIME + 2 * CHECK_TIME)

/* GUARD_TIME is the time before the expected phase of a neighbor that
   a transmitted should begin transmitting packets. */
#define GUARD_TIME                         10 * CHECK_TIME + CHECK_TIME_TX

/* INTER_PACKET_INTERVAL is the interval between two successive packet transmissions */
#define INTER_PACKET_INTERVAL              RTIMER_ARCH_SECOND / 5000

/* AFTER_ACK_DETECTECT_WAIT_TIME is the time to wait after a potential
   ACK packet has been detected until we can read it out from the
   radio. */
#define AFTER_ACK_DETECTECT_WAIT_TIME      RTIMER_ARCH_SECOND / 1500

/* MAX_PHASE_STROBE_TIME is the time that we transmit repeated packets
   to a neighbor for which we have a phase lock. */
#define MAX_PHASE_STROBE_TIME              RTIMER_ARCH_SECOND / 60


/* SHORTEST_PACKET_SIZE is the shortest packet that ContikiMAC
   allows. Packets have to be a certain size to be able to be detected
   by two consecutive CCA checks, and here is where we define this
   shortest size.
   Padded packets will have the wrong ipv6 checksum unless CONTIKIMAC_HEADER
   is used (on both sides) and the receiver will ignore them.
   With no header, reduce to transmit a proper multicast RPL DIS. */
#ifdef CONTIKIMAC_CONF_SHORTEST_PACKET_SIZE
#define SHORTEST_PACKET_SIZE  CONTIKIMAC_CONF_SHORTEST_PACKET_SIZE
#else
#define SHORTEST_PACKET_SIZE               43
#endif


#define ACK_LEN 3 + EXTRA_ACK_LEN

#include <stdio.h>
static struct rtimer rt;
static struct pt pt;

static volatile uint8_t contikimac_is_on = 0;
volatile uint8_t contikimac_keep_radio_on = 0;

volatile unsigned char we_are_sending = 0;
static volatile unsigned char radio_is_on = 0;

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTDEBUG(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#define PRINTDEBUG(...)
#endif

#if CONTIKIMAC_CONF_COMPOWER
static struct compower_activity current_packet;
#endif /* CONTIKIMAC_CONF_COMPOWER */

#if WITH_PHASE_OPTIMIZATION

#include "net/mac/phase.h"

#ifdef CONTIKIMAC_CONF_MAX_PHASE_NEIGHBORS
#define MAX_PHASE_NEIGHBORS CONTIKIMAC_CONF_MAX_PHASE_NEIGHBORS
#endif

#ifndef MAX_PHASE_NEIGHBORS
#define MAX_PHASE_NEIGHBORS 30
#endif

PHASE_LIST(phase_list, MAX_PHASE_NEIGHBORS);

#endif /* WITH_PHASE_OPTIMIZATION */

#define DEFAULT_STREAM_TIME (4 * CYCLE_TIME)

#ifndef MIN
#define MIN(a, b) ((a) < (b)? (a) : (b))
#endif /* MIN */

struct seqno {
  rimeaddr_t sender;
  uint8_t seqno;
};

struct app_seqno {
  uint32_t seqno;
};

#ifdef NETSTACK_CONF_MAC_SEQNO_HISTORY
#define MAX_SEQNOS_LL NETSTACK_CONF_MAC_SEQNO_HISTORY
#else /* NETSTACK_CONF_MAC_SEQNO_HISTORY */
#define MAX_SEQNOS_LL 16
#endif /* NETSTACK_CONF_MAC_SEQNO_HISTORY */
static struct seqno received_seqnos[MAX_SEQNOS_LL];
/* App-layer duplicate detection. Done at RDC layer for simplicity. */
#define MAX_SEQNOS_APP 32
static struct app_seqno received_app_seqnos[MAX_SEQNOS_APP];

#if CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT
static struct timer broadcast_rate_timer;
static int broadcast_rate_counter;
#endif /* CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT */

/*---------------------------------------------------------------------------*/
static void
on(void)
{
  if(contikimac_is_on && radio_is_on == 0) {
    radio_is_on = 1;
    NETSTACK_RADIO.on();
  }
}
/*---------------------------------------------------------------------------*/
static void
off(void)
{
  if(contikimac_is_on && radio_is_on != 0 &&
     contikimac_keep_radio_on == 0) {
    radio_is_on = 0;
    NETSTACK_RADIO.off();
  }
}
/*---------------------------------------------------------------------------*/
static volatile rtimer_clock_t cycle_start;
static char powercycle(struct rtimer *t, void *ptr);
static void
schedule_powercycle(struct rtimer *t, rtimer_clock_t time)
{
  int r;

  if(contikimac_is_on) {

    if(RTIMER_CLOCK_LT(RTIMER_TIME(t) + time, RTIMER_NOW() + 2)) {
      time = RTIMER_NOW() - RTIMER_TIME(t) + 2;
    }

    r = rtimer_set(t, RTIMER_TIME(t) + time, 1,
                   (void (*)(struct rtimer *, void *))powercycle, NULL);
    if(r != RTIMER_OK) {
      printf("schedule_powercycle: could not set rtimer\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
static void
schedule_powercycle_fixed(struct rtimer *t, rtimer_clock_t fixed_time)
{
  int r;

  if(contikimac_is_on) {

    if(RTIMER_CLOCK_LT(fixed_time, RTIMER_NOW() + 1)) {
      fixed_time = RTIMER_NOW() + 1;
    }

    r = rtimer_set(t, fixed_time, 1,
                   (void (*)(struct rtimer *, void *))powercycle, NULL);
    if(r != RTIMER_OK) {
      printf("schedule_powercycle: could not set rtimer\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
static void
powercycle_turn_radio_off(void)
{
#if CONTIKIMAC_CONF_COMPOWER
  uint8_t was_on = radio_is_on;
#endif /* CONTIKIMAC_CONF_COMPOWER */
  
  if(we_are_sending == 0 && we_are_receiving_burst == 0) {
    off();
#if CONTIKIMAC_CONF_COMPOWER
    if(was_on && !radio_is_on) {
      compower_accumulate(&compower_idle_activity);
    }
#endif /* CONTIKIMAC_CONF_COMPOWER */
  }
}
/*---------------------------------------------------------------------------*/
static void
powercycle_turn_radio_on(void)
{
  if(we_are_sending == 0 && we_are_receiving_burst == 0) {
    on();
  }
}
extern uint32_t packet_seen_count;
extern uint32_t sfd_decoded_count;
/*---------------------------------------------------------------------------*/
static char
powercycle(struct rtimer *t, void *ptr)
{
  PT_BEGIN(&pt);

#if SYNC_CYCLE_STARTS
static volatile rtimer_clock_t sync_cycle_start;
static volatile uint8_t sync_cycle_phase;
  sync_cycle_start = RTIMER_NOW();
#else
  cycle_start = RTIMER_NOW();
#endif

  while(1) {

    static uint8_t packet_seen;
    static uint8_t sfd_decoded;
    static rtimer_clock_t t0;
    static uint16_t count;

#if SYNC_CYCLE_STARTS
    /* Compute cycle start when RTIMER_ARCH_SECOND is not a multiple of CHANNEL_CHECK_RATE */
    if (sync_cycle_phase++ == NETSTACK_RDC_CHANNEL_CHECK_RATE) {
       sync_cycle_phase = 0;
       sync_cycle_start += RTIMER_ARCH_SECOND;
       cycle_start = sync_cycle_start;
    } else {
#if (RTIMER_ARCH_SECOND * NETSTACK_RDC_CHANNEL_CHECK_RATE) > 65535
       cycle_start = sync_cycle_start + ((unsigned long)(sync_cycle_phase*RTIMER_ARCH_SECOND))/NETSTACK_RDC_CHANNEL_CHECK_RATE;
#else
       cycle_start = sync_cycle_start + (sync_cycle_phase*RTIMER_ARCH_SECOND)/NETSTACK_RDC_CHANNEL_CHECK_RATE;
#endif
    }
#else
    cycle_start += CYCLE_TIME;
#endif

    packet_seen = 0;
    sfd_decoded = 0;

    for(count = 0; count < CCA_COUNT_MAX; ++count) {
      t0 = RTIMER_NOW();
      if(we_are_sending == 0 && we_are_receiving_burst == 0) {
        powercycle_turn_radio_on();
        /* Check if a packet is seen in the air. If so, we keep the
             radio on for a while (LISTEN_TIME_AFTER_PACKET_DETECTED) to
             be able to receive the packet. We also continuously check
             the radio medium to make sure that we wasn't woken up by a
             false positive: a spurious radio interference that was not
             caused by an incoming packet. */
        if(NETSTACK_RADIO.channel_clear() == 0) {
          packet_seen = 1;
          break;
        }
        powercycle_turn_radio_off();
      }
      schedule_powercycle_fixed(t, RTIMER_NOW() + CCA_SLEEP_TIME);
      PT_YIELD(&pt);
    }

    if(packet_seen) {
      static rtimer_clock_t start;
      static uint8_t silence_periods, periods;
      start = RTIMER_NOW();

      periods = silence_periods = 0;
      while(we_are_sending == 0 && radio_is_on &&
            RTIMER_CLOCK_LT(RTIMER_NOW(),
                            (start + LISTEN_TIME_AFTER_PACKET_DETECTED))) {

        /* Check for a number of consecutive periods of
             non-activity. If we see two such periods, we turn the
             radio off. Also, if a packet has been successfully
             received (as indicated by the
             NETSTACK_RADIO.pending_packet() function), we stop
             snooping. */
#if !RDC_CONF_HARDWARE_CSMA
       /* A cca cycle will disrupt rx on some radios, e.g. mc1322x, rf230 */
       /*TODO: Modify those drivers to just return the internal RSSI when already in rx mode */
        if(NETSTACK_RADIO.channel_clear()) {
          ++silence_periods;
        } else {
          silence_periods = 0;
        }
#endif

        ++periods;

        if(NETSTACK_RADIO.receiving_packet()) {
          silence_periods = 0;
          sfd_decoded = 1;
        }
        if(silence_periods > MAX_SILENCE_PERIODS) {
          powercycle_turn_radio_off();
          break;
        }
        if(WITH_FAST_SLEEP &&
            periods > MAX_NONACTIVITY_PERIODS &&
            !(NETSTACK_RADIO.receiving_packet() ||
              NETSTACK_RADIO.pending_packet())) {
          powercycle_turn_radio_off();
          break;
        }
        if(NETSTACK_RADIO.pending_packet()) {
          sfd_decoded = 1;
          break;
        }

        schedule_powercycle(t, CCA_CHECK_TIME + CCA_SLEEP_TIME);
        PT_YIELD(&pt);
      }

      if(radio_is_on) {
        if(!(NETSTACK_RADIO.receiving_packet() ||
             NETSTACK_RADIO.pending_packet()) ||
             !RTIMER_CLOCK_LT(RTIMER_NOW(),
                 (start + LISTEN_TIME_AFTER_PACKET_DETECTED))) {
          powercycle_turn_radio_off();
        }
      }
    }

    if(packet_seen) {
      packet_seen_count++;
    }
    if(sfd_decoded) {
      sfd_decoded_count++;
    }

    if(RTIMER_CLOCK_LT(RTIMER_NOW() - cycle_start, CYCLE_TIME - CHECK_TIME * 4)) {
	     /* Schedule the next powercycle interrupt, or sleep the mcu until then.
                Sleeping will not exit from this interrupt, so ensure an occasional wake cycle
				or foreground processing will be blocked until a packet is detected */
#if RDC_CONF_MCU_SLEEP
      static uint8_t sleepcycle;
      if ((sleepcycle++<16) && !we_are_sending && !radio_is_on) {
        rtimer_arch_sleep(CYCLE_TIME - (RTIMER_NOW() - cycle_start));
      } else {
        sleepcycle = 0;
        schedule_powercycle_fixed(t, CYCLE_TIME + cycle_start);
        PT_YIELD(&pt);
      }
#else

#if WITH_CONTIKIMIAC_JITTER
//      schedule_powercycle_fixed(t, (random_rand() % CYCLE_TIME) + cycle_start);
//      schedule_powercycle_fixed(t, CYCLE_TIME + cycle_start);
//      schedule_powercycle(t, CYCLE_TIME - (random_rand() % (CYCLE_TIME/32)));
      schedule_powercycle(t, CYCLE_TIME - (random_rand() % (CYCLE_TIME/8)));
#else
      schedule_powercycle_fixed(t, CYCLE_TIME + cycle_start);
#endif
      PT_YIELD(&pt);
#endif
    }
  }

  PT_END(&pt);
}
/*---------------------------------------------------------------------------*/
static int
broadcast_rate_drop(void)
{
#if CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT
  if(!timer_expired(&broadcast_rate_timer)) {
    broadcast_rate_counter++;
    if(broadcast_rate_counter < CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT) {
      return 0;
    } else {
      return 1;
    }
  } else {
    timer_set(&broadcast_rate_timer, CLOCK_SECOND);
    broadcast_rate_counter = 0;
    return 0;
  }
#else /* CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT */
  return 0;
#endif /* CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT */
}
/*---------------------------------------------------------------------------*/
static int
send_packet(mac_callback_t mac_callback, void *mac_callback_ptr, struct rdc_buf_list *buf_list)
{
  static int collision_count;
  rtimer_clock_t t0;
  rtimer_clock_t encounter_time = 0, previous_txtime = 0;
  int strobes;
  uint8_t got_strobe_ack = 0;
  int hdrlen, len;
  uint8_t is_broadcast = 0;
  uint8_t collisions;
  int transmit_len;
  int ret;
  uint8_t contikimac_was_on;
  uint8_t seqno;
#if WITH_CONTIKIMAC_HEADER
  struct hdr *chdr;
#endif /* WITH_CONTIKIMAC_HEADER */

 /* Exit if RDC and radio were explicitly turned off */
   if (!contikimac_is_on && !contikimac_keep_radio_on) {
    printf("contikimac: radio is turned off MAC_ERROR\n");
    return MAC_TX_ERR_FATAL;
  }
  
  if(packetbuf_totlen() == 0) {
    printf("contikimac: send_packet data len 0 MAC_ERROR\n");
    return MAC_TX_ERR_FATAL;
  }

  packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &rimeaddr_node_addr);
  if(rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER), &rimeaddr_null)) {
    is_broadcast = 1;
    PRINTDEBUG("contikimac: send broadcast\n");

    if(broadcast_rate_drop()) {
      collision_count++;
      return MAC_TX_COLLISION;
    }
  } else {

    /* set anycast address including a most fresh e2e_edc (metric) */
    anycast_set_packetbuf_addr();

#if UIP_CONF_IPV6
    PRINTDEBUG("contikimac: send unicast to %02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[1],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[2],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[3],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[4],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[5],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[6],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[7]);
#else /* UIP_CONF_IPV6 */
    PRINTDEBUG("contikimac: send unicast to %u.%u\n",
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[1]);
#endif /* UIP_CONF_IPV6 */
  }
//  is_reliable = packetbuf_attr(PACKETBUF_ATTR_RELIABLE) ||
//    packetbuf_attr(PACKETBUF_ATTR_ERELIABLE);

  packetbuf_set_attr(PACKETBUF_ATTR_MAC_ACK, 1);

#if WITH_CONTIKIMAC_HEADER
  hdrlen = packetbuf_totlen();
  if(packetbuf_hdralloc(sizeof(struct hdr)) == 0) {
    /* Failed to allocate space for contikimac header */
    printf("contikimac: send failed, too large header MAC_ERROR\n");
    return MAC_TX_ERR_FATAL;
  }
  chdr = packetbuf_hdrptr();
  chdr->id = CONTIKIMAC_ID;
  chdr->len = hdrlen;
  
  /* Create the MAC header for the data packet. */
  hdrlen = NETSTACK_FRAMER.create();
  if(hdrlen < 0) {
    /* Failed to send */
    printf("contikimac: send failed, too large header MAC_ERROR\n");
    packetbuf_hdr_remove(sizeof(struct hdr));
    return MAC_TX_ERR_FATAL;
  }
  hdrlen += sizeof(struct hdr);
#else
  /* Create the MAC header for the data packet. */
  hdrlen = NETSTACK_FRAMER.create();
  if(hdrlen < 0) {
    /* Failed to send */
    printf("contikimac: send failed, too large header MAC_ERROR\n");
    return MAC_TX_ERR_FATAL;
  }
#endif


  /* Make sure that the packet is longer or equal to the shortest
     packet length. */
  transmit_len = packetbuf_totlen();
  if(transmit_len < SHORTEST_PACKET_SIZE) {
    /* Pad with zeroes */
    uint8_t *ptr;
    ptr = packetbuf_dataptr();
    memset(ptr + packetbuf_datalen(), 0, SHORTEST_PACKET_SIZE - packetbuf_totlen());

    PRINTF("contikimac: shorter than shortest (%d)\n", packetbuf_totlen());
    transmit_len = SHORTEST_PACKET_SIZE;
  }


  packetbuf_compact();

#ifdef NETSTACK_ENCRYPT
  NETSTACK_ENCRYPT();
#endif /* NETSTACK_ENCRYPT */

  transmit_len = packetbuf_totlen();

  NETSTACK_RADIO.prepare(packetbuf_hdrptr(), transmit_len);

  /* Remove the MAC-layer header since it will be recreated next time around. */
  packetbuf_hdr_remove(hdrlen);

  if(!is_broadcast && !is_receiver_awake) {
#if WITH_PHASE_OPTIMIZATION
    ret = phase_wait(&phase_list, packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                     CYCLE_TIME, GUARD_TIME,
                     mac_callback, mac_callback_ptr, buf_list);
    if(ret == PHASE_DEFERRED) {
      return MAC_TX_DEFERRED;
    }
    if(ret != PHASE_UNKNOWN) {
      is_known_receiver = 1;
    }
#endif /* WITH_PHASE_OPTIMIZATION */ 
  }
  


  /* By setting we_are_sending to one, we ensure that the rtimer
     powercycle interrupt do not interfere with us sending the packet. */
  we_are_sending = 1;

  /* If we have a pending packet in the radio, we should not send now,
     because we will trash the received packet. Instead, we signal
     that we have a collision, which lets the packet be received. This
     packet will be retransmitted later by the MAC protocol
     instread. */
  if(NETSTACK_RADIO.receiving_packet() || NETSTACK_RADIO.pending_packet()) {
    we_are_sending = 0;
    PRINTF("contikimac: collision receiving %d, pending %d\n",
           NETSTACK_RADIO.receiving_packet(), NETSTACK_RADIO.pending_packet());
    collision_count++;
    return MAC_TX_COLLISION;
  }
  
  /* Switch off the radio to ensure that we didn't start sending while
     the radio was doing a channel check. */
  off();


  strobes = 0;

  /* Send a train of strobes until the receiver answers with an ACK. */
  collisions = 0;

  got_strobe_ack = 0;

  /* Set contikimac_is_on to one to allow the on() and off() functions
     to control the radio. We restore the old value of
     contikimac_is_on when we are done. */
  contikimac_was_on = contikimac_is_on;
  contikimac_is_on = 1;

#if !RDC_CONF_HARDWARE_CSMA
    /* Check if there are any transmissions by others. */
    /* TODO: why does this give collisions before sending with the mc1322x? */
  if(is_receiver_awake == 0) {
	int i;
    for(i = 0; i < CCA_COUNT_MAX_TX; ++i) {
      t0 = RTIMER_NOW();
      on();
#if CCA_CHECK_TIME > 0
      while(RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + CCA_CHECK_TIME)) { }
#endif
      if(NETSTACK_RADIO.channel_clear() == 0) {
        collisions++;
        off();
        break;
      }
      off();
      t0 = RTIMER_NOW();
      while(RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + CCA_SLEEP_TIME)) { }
    }
  }

  if(collisions > 0) {
    we_are_sending = 0;
    off();
    PRINTF("contikimac: collisions before sending\n");
//    printf("Cmac:! collisions before sending %d", collisions);
//    rpl_trace(rpl_dataptr_from_packetbuf());

    uint16_t edc_inc = 1;
    /* Accumulate strobe duration over multiple CSMA transmissions to get a correct EDC value */
    uint16_t edc = packetbuf_attr(PACKETBUF_ATTR_EDC) + edc_inc;
    packetbuf_set_attr(PACKETBUF_ATTR_EDC, edc);

    contikimac_is_on = contikimac_was_on;
    collision_count++;
    return MAC_TX_COLLISION;
  }
#endif /* RDC_CONF_HARDWARE_CSMA */

#if !RDC_CONF_HARDWARE_ACK
  /* if(!is_broadcast) */ {
  /* Turn radio on to receive expected unicast ack.
      Not necessary with hardware ack detection, and may trigger an unnecessary cca or rx cycle */	 
     on();
  }
#endif

  watchdog_periodic();
  seqno = packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);

  uint8_t ackbuf[ACK_LEN];

  rimeaddr_t *dest = NULL;

  t0 = RTIMER_NOW();
//#define MAX_COLLISIONS 32
//  for(strobes = 0, collisions = 0;
//      got_strobe_ack == 0 /*&& collisions < MAX_COLLISIONS*/ &&
//      RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + STROBE_TIME); strobes++) {
    for(strobes = 0, collisions = 0;
          (is_broadcast || (/*got_strobe_ack == 0 &&*/ collisions == 0)) &&
          RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + STROBE_TIME); strobes++) {

    watchdog_periodic();

//    if((is_receiver_awake || is_known_receiver) && !RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + MAX_PHASE_STROBE_TIME)) {
//      PRINTF("miss to %d\n", packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0]);
//      break;
//    }

    len = 0;

    previous_txtime = RTIMER_NOW();

    {
      rtimer_clock_t wt;
      rtimer_clock_t txtime;
      int ret;

      txtime = RTIMER_NOW();

      ret = NETSTACK_RADIO.transmit(transmit_len);

#if RDC_CONF_HARDWARE_ACK
     /* For radios that block in the transmit routine and detect the ACK in hardware */
      if(ret == RADIO_TX_OK) {
        if(!is_broadcast) {
          got_strobe_ack = 1;
          encounter_time = previous_txtime;
          break;
        }
      } else if (ret == RADIO_TX_NOACK) {
      } else if (ret == RADIO_TX_COLLISION) {
          PRINTF("contikimac: collisions while sending\n");
          collisions++;
      }
	  wt = RTIMER_NOW();
      while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + INTER_PACKET_INTERVAL)) { }
#else
     /* Wait for the ACK packet */

      wt = RTIMER_NOW();
      NETSTACK_RADIO.on();
      while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + INTER_PACKET_INTERVAL)) { }

//      if(!is_broadcast && (NETSTACK_RADIO.receiving_packet() ||
//                           NETSTACK_RADIO.pending_packet() ||
//                           NETSTACK_RADIO.channel_clear() == 0)) {

      int t1 = NETSTACK_RADIO.receiving_packet();
      int t2 = NETSTACK_RADIO.pending_packet();
      int t3 = NETSTACK_RADIO.channel_clear() == 0;
      if(/*!is_broadcast &&*/ ( t1 ||
                                 t2 ||
                                 t3 )) {

        wt = RTIMER_NOW();
        while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + AFTER_ACK_DETECTECT_WAIT_TIME)) { }

        len = NETSTACK_RADIO.read(ackbuf, ACK_LEN);
#if SELECT_ACK
        int expected_len = is_broadcast ? ACK_LEN : 3;
#else
        int expected_len = ACK_LEN;
#endif

        if(len == expected_len && seqno == ackbuf[2]) {
          if(is_broadcast) {
            got_strobe_ack++;
            encounter_time = previous_txtime;
            dest = (rimeaddr_t *)(ackbuf+3);
            uint16_t neighbor_rank = (ackbuf[3+8+1]<<8) + ackbuf[3+8];
            anycast_update_neighbor_edc(dest, neighbor_rank);
            broadcast_acked(dest);
          } else {
            got_strobe_ack++;
            encounter_time = previous_txtime;
#if SELECT_ACK
            dest = NULL;
#else
            dest = (rimeaddr_t *)(ackbuf+3);
            uint16_t neighbor_rank = (ackbuf[3+8+1]<<8) + ackbuf[3+8];
            anycast_update_neighbor_edc(dest, neighbor_rank);
#endif
            if(got_strobe_ack >= 1) break;
          }
//          dest = ackbuf+3;
//          uint16_t neighbor_rank = (ackbuf[3+8+1]<<8) + ackbuf[3+8];
//          anycast_update_neighbor_edc(dest, neighbor_rank);
//          if(!is_broadcast) {
//        	  /* Set packetbuf receiver */
//    	      packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, dest);
//        	  break;
//          } else {
//        	  broadcast_acked(dest);
//          }
//        } else if(t1 || t3) {
////          collided_packet_len = len;
////          printf("Cmac:! collisions while sending %d %d %d\n", t1, t2, t3);
//          collisions++;
        }
      }
#endif /* RDC_CONF_HARDWARE_ACK */

      previous_txtime = txtime;
    }
  }

  if(is_broadcast) {
	  broadcast_done();
  }

  uint16_t strobe_duration = EDC_TICKS_TO_METRIC(RTIMER_NOW() - t0);
  uint16_t edc_inc = strobe_duration;
  if(edc_inc < EDC_DIVISOR/16) {
    edc_inc = EDC_DIVISOR/16; /* Min "penalty" for any attempted tx */
  }
  /* Accumulate strobe duration over multiple CSMA transmissions to get a correct EDC value */
  uint16_t edc = packetbuf_attr(PACKETBUF_ATTR_EDC) + edc_inc;
  packetbuf_set_attr(PACKETBUF_ATTR_EDC, edc);

  off();

  PRINTF("contikimac: send (strobes=%u, len=%u, %s, %d), done\n", strobes,
         packetbuf_totlen(),
         got_strobe_ack ? "ack" : "no ack",
         collisions);

#if CONTIKIMAC_CONF_COMPOWER
  /* Accumulate the power consumption for the packet transmission. */
  compower_accumulate(&current_packet);

  /* Convert the accumulated power consumption for the transmitted
     packet to packet attributes so that the higher levels can keep
     track of the amount of energy spent on transmitting the
     packet. */
  compower_attrconv(&current_packet);

  /* Clear the accumulated power consumption so that it is ready for
     the next packet. */
  compower_clear(&current_packet);
#endif /* CONTIKIMAC_CONF_COMPOWER */

  contikimac_is_on = contikimac_was_on;
  we_are_sending = 0;

  /* Determine the return value that we will return from the
     function. We must pass this value to the phase module before we
     return from the function.  */
  if(collisions > 0) {
    ret = MAC_TX_COLLISION;
    collision_count++;
  } else if(!is_broadcast && !got_strobe_ack) {
    ret = MAC_TX_NOACK;
  } else {
    ret = MAC_TX_OK;
  }

#if WITH_PHASE_OPTIMIZATION

  if(is_known_receiver && got_strobe_ack) {
    PRINTF("no miss %d wake-ups %d\n", packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0],
           strobes);
  }

  if(!is_broadcast) {
    if(collisions < MAX_COLLISIONS && is_receiver_awake == 0) {
      phase_update(&phase_list, packetbuf_addr(PACKETBUF_ADDR_RECEIVER), encounter_time,
                   ret);
    }
  }
#endif /* WITH_PHASE_OPTIMIZATION */

  if(!is_broadcast) {
    if(got_strobe_ack) {
      struct app_data *dataptr = rpl_dataptr_from_packetbuf();
//#if ACK_WITH_ADDR
      printf("Cmac: acked by %u s %u c %d seq %u",
          node_id_from_rimeaddr(dest),
          strobe_duration,
          collision_count, seqno);
//#else
//      printf("Cmac: acked strobe %u collision count %d seqno %u", strobe_duration, collision_count, seqno);
//#endif
      rpl_trace(dataptr);
      if(dataptr && !packetbuf_attr(PACKETBUF_ATTR_GOING_UP)) {
        struct app_data data;
        app_data_init(&data, dataptr);
        acked_down_insert(data.seqno, node_id_from_rimeaddr(dest));
      }
    } else {
        printf("Cmac:! noack s %u c %d seq %u", strobe_duration, collisions, seqno);
        rpl_trace(rpl_dataptr_from_packetbuf());
    }
  }

  if(ret != MAC_TX_COLLISION) {
    collision_count = 0;
  }

  return ret;
}
/*---------------------------------------------------------------------------*/
static void
qsend_packet(mac_callback_t sent, void *ptr)
{
  int ret = send_packet(sent, ptr, NULL);
  if(ret != MAC_TX_DEFERRED) {
    mac_call_sent_callback(sent, ptr, ret, 1);
  }
}
/*---------------------------------------------------------------------------*/
static void
qsend_list(mac_callback_t sent, void *ptr, struct rdc_buf_list *buf_list)
{
  struct rdc_buf_list *curr = buf_list;
  struct rdc_buf_list *next;
  int ret;
  if(curr == NULL) {
    return;
  }
  /* Do not send during reception of a burst */
  if(we_are_receiving_burst) {
    /* Prepare the packetbuf for callback */
    queuebuf_to_packetbuf(curr->buf);
    /* Return COLLISION so the MAC may try again later */
    mac_call_sent_callback(sent, ptr, MAC_TX_COLLISION, 1);
    return;
  }
  /* The receiver needs to be awoken before we send */
  is_receiver_awake = 0;
  do { /* A loop sending a burst of packets from buf_list */
//    next = list_item_next(curr);
    next = NULL;

    /* Prepare the packetbuf */
    queuebuf_to_packetbuf(curr->buf);
    if(next != NULL) {
      packetbuf_set_attr(PACKETBUF_ATTR_PENDING, 1);
    }

    /* Send the current packet */
    ret = send_packet(sent, ptr, curr);
    if(ret != MAC_TX_DEFERRED) {
      mac_call_sent_callback(sent, ptr, ret, 1);
    }

    if(ret == MAC_TX_OK) {
      if(next != NULL) {
        /* We're in a burst, no need to wake the receiver up again */
        is_receiver_awake = 1;
        curr = next;
      }
    } else {
      /* The transmission failed, we stop the burst */
      next = NULL;
    }
  } while(next != NULL);
  is_receiver_awake = 0;
}
/*---------------------------------------------------------------------------*/
/* Timer callback triggered when receiving a burst, after having waited for a next
   packet for a too long time. Turns the radio off and leaves burst reception mode */
static void
recv_burst_off(void *ptr)
{
  off();
  we_are_receiving_burst = 0;
}
/*---------------------------------------------------------------------------*/
static void
input_packet(void)
{
  static struct ctimer ct;
  if(!we_are_receiving_burst) {
    off();
  }

  /*  printf("cycle_start 0x%02x 0x%02x\n", cycle_start, cycle_start % CYCLE_TIME);*/

#ifdef NETSTACK_DECRYPT
  NETSTACK_DECRYPT();
#endif /* NETSTACK_DECRYPT */

  if(packetbuf_totlen() > 0 && NETSTACK_FRAMER.parse() >= 0) {

#if WITH_CONTIKIMAC_HEADER
    struct hdr *chdr;
    chdr = packetbuf_dataptr();
    if(chdr->id != CONTIKIMAC_ID) {
      PRINTF("contikimac: failed to parse hdr (%u)\n", packetbuf_totlen());
      printf("contikimac: failed to parse hdr (%u)\n", packetbuf_totlen());
      return;
    }
    packetbuf_hdrreduce(sizeof(struct hdr));
    packetbuf_set_datalen(chdr->len);
#endif /* WITH_CONTIKIMAC_HEADER */

    if(packetbuf_datalen() > 0 &&
       packetbuf_totlen() > 0 &&
       (rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                     &rimeaddr_node_addr) ||
        rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                     &rimeaddr_null)) &&
       (!(packetbuf_attr(PACKETBUF_ATTR_IS_ANYCAST) && !packetbuf_attr(PACKETBUF_ATTR_DO_ACK))) /* Anycast that we don't ack are not for us */
    ) {
      /* This is a regular packet that is destined to us or to the
         broadcast address. */

      /* If FRAME_PENDING is set, we are receiving a packets in a burst */
      we_are_receiving_burst = packetbuf_attr(PACKETBUF_ATTR_PENDING);
      if(we_are_receiving_burst) {
        on();
        /* Set a timer to turn the radio off in case we do not receive a next packet */
        ctimer_set(&ct, INTER_PACKET_DEADLINE, recv_burst_off, NULL);
      } else {
        off();
        ctimer_stop(&ct);
      }

      struct app_data *dataptr = NULL;
      if(packetbuf_attr(PACKETBUF_ATTR_IS_ANYCAST)) {
        dataptr = rpl_dataptr_from_packetbuf();
      }
      struct app_data data;
      app_data_init(&data, dataptr);

      /* Check for duplicate packet by comparing the sequence number
         of the incoming packet with the last few ones we saw. */
      if(rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER), /* duplicate detection for broadcast only */
                                    &rimeaddr_null))
      {
        int i;
        for(i = 0; i < MAX_SEQNOS_LL; ++i) {
          if(packetbuf_attr(PACKETBUF_ATTR_PACKET_ID) == received_seqnos[i].seqno &&
             rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_SENDER),
                          &received_seqnos[i].sender)) {
            /* Drop the packet. */
            /*        printf("Drop duplicate ContikiMAC layer packet\n");*/
            return;
          }
        }
        for(i = MAX_SEQNOS_LL - 1; i > 0; --i) {
          memcpy(&received_seqnos[i], &received_seqnos[i - 1],
                 sizeof(struct seqno));
        }
        received_seqnos[0].seqno = packetbuf_attr(PACKETBUF_ATTR_PACKET_ID);
        rimeaddr_copy(&received_seqnos[0].sender,
                      packetbuf_addr(PACKETBUF_ADDR_SENDER));
      }

#if CONTIKIMAC_CONF_COMPOWER
      /* Accumulate the power consumption for the packet reception. */
      compower_accumulate(&current_packet);
      /* Convert the accumulated power consumption for the received
         packet to packet attributes so that the higher levels can
         keep track of the amount of energy spent on receiving the
         packet. */
      compower_attrconv(&current_packet);

      /* Clear the accumulated power consumption so that it is ready
         for the next packet. */
      compower_clear(&current_packet);
#endif /* CONTIKIMAC_CONF_COMPOWER */

      PRINTDEBUG("contikimac: data (%u)\n", packetbuf_datalen());
      
      if(dataptr && packetbuf_attr(PACKETBUF_ATTR_IS_ANYCAST)) {

        uint32_t seqno = data.seqno;

        dataptr->hop += 1;
        if(dataptr->hop > 128) {
          printf("Cmac: dropping from %d after too many hops", node_id_from_rimeaddr(packetbuf_addr(PACKETBUF_ADDR_SENDER)));
          rpl_trace(dataptr);
          return;
        }

        /* Duplicate detection */
        {
          int i;
          if(!packetbuf_attr(PACKETBUF_ATTR_IS_RECOVERY)) {
            for(i = 0; i < MAX_SEQNOS_APP; ++i) {
              /* base the comparison on both seqno and false-positive count, so that fp recovery packet
               * are not dropped as app-layer duplicates */
              if(seqno == received_app_seqnos[i].seqno) {
                /* Drop the packet. */
                printf("Cmac:! dropping app-layer duplicate from %d", node_id_from_rimeaddr(packetbuf_addr(PACKETBUF_ADDR_SENDER)));
                rpl_trace(dataptr);
                return;
              }
            }
          }
          for(i = MAX_SEQNOS_APP - 1; i > 0; --i) {
            received_app_seqnos[i] = received_app_seqnos[i - 1];
          }
          received_app_seqnos[0].seqno = seqno;
        }

        printf("Cmac: input from %d nd %u",
            node_id_from_rimeaddr(packetbuf_addr(PACKETBUF_ADDR_SENDER)),
            packetbuf_attr(PACKETBUF_ATTR_DUP_COUNT)
        );
        rpl_trace(dataptr);
      }

      NETSTACK_MAC.input();

      return;
    } else {
      PRINTDEBUG("contikimac: data not for us\n");
      if(packetbuf_attr(PACKETBUF_ATTR_IS_ANYCAST)) {
        static uint8_t prev_seqno = 0;
        if(packetbuf_attr(PACKETBUF_ATTR_PACKET_ID) != prev_seqno) {
          printf("Cmac: not for us src %u rank %u\n", node_id_from_rimeaddr(packetbuf_addr(PACKETBUF_ADDR_SENDER)), packetbuf_attr(PACKETBUF_ATTR_EDC));
          anycast_update_neighbor_edc(packetbuf_addr(PACKETBUF_ADDR_SENDER), packetbuf_attr(PACKETBUF_ATTR_EDC));
          prev_seqno = packetbuf_attr(PACKETBUF_ATTR_PACKET_ID);
        }
      }
//      printf("contikimac: data not for us %d %d %d %d %d ", packetbuf_datalen(), packetbuf_totlen(), packetbuf_attr(PACKETBUF_ATTR_IS_ANYCAST), packetbuf_attr(PACKETBUF_ATTR_DO_ACK), packetbuf_attr(PACKETBUF_ATTR_PACKET_ID));
//      uip_debug_lladdr_print(packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
//      printf("\n");
//      rpl_trace(rpl_dataptr_from_packetbuf());
    }
  } else {
    if(packetbuf_totlen() > 0) printf("Cmac: failed to parse (%u)\n", packetbuf_totlen());
  }
}
static int
turn_off(int keep_radio_on);
/*---------------------------------------------------------------------------*/
static void
init(void)
{
  radio_is_on = 0;
  PT_INIT(&pt);

  rtimer_set(&rt, RTIMER_NOW() + (random_rand() % CYCLE_TIME), 1,
             (void (*)(struct rtimer *, void *))powercycle, NULL);

  contikimac_is_on = 1;

//  if(node_id == 1) turn_off(1);

#if WITH_PHASE_OPTIMIZATION
  phase_init(&phase_list);
#endif /* WITH_PHASE_OPTIMIZATION */

}
/*---------------------------------------------------------------------------*/
static int
turn_on(void)
{
  if(contikimac_is_on == 0) {
    contikimac_is_on = 1;
    contikimac_keep_radio_on = 0;
    rtimer_set(&rt, RTIMER_NOW() + CYCLE_TIME, 1,
               (void (*)(struct rtimer *, void *))powercycle, NULL);
  }
  return 1;
}
/*---------------------------------------------------------------------------*/
static int
turn_off(int keep_radio_on)
{
  contikimac_is_on = 0;
  contikimac_keep_radio_on = keep_radio_on;
  if(keep_radio_on) {
    radio_is_on = 1;
    return NETSTACK_RADIO.on();
  } else {
    radio_is_on = 0;
    return NETSTACK_RADIO.off();
  }
}
/*---------------------------------------------------------------------------*/
static unsigned short
duty_cycle(void)
{
  return (1ul * CLOCK_SECOND * CYCLE_TIME) / RTIMER_ARCH_SECOND;
}
/*---------------------------------------------------------------------------*/
const struct rdc_driver contikimac_driver = {
  "ContikiMAC",
  init,
  qsend_packet,
  qsend_list,
  input_packet,
  turn_on,
  turn_off,
  duty_cycle,
};
/*---------------------------------------------------------------------------*/
uint16_t
contikimac_debug_print(void)
{
  return 0;
}
/*---------------------------------------------------------------------------*/
