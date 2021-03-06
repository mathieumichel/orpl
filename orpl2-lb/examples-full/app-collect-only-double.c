/*
 * Copyright (c) 2013, Swedish Institute of Computer Science.
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
 */
/**
 * \file
 *         Example file using ORPL for a data collection.
 *         Enables logging as used in the ORPL SenSyS'13 paper.
 *         Can be deployed in the Indriya or Twist testbeds.
 *
 * \author Simon Duquennoy <simonduq@sics.se>
 */

#include "contiki-conf.h"
#include "lib/random.h"
#include "orpl.h"
#include "deployment.h"
#include "simple-energest.h"
#include "simple-udp.h"
#include "cc2420.h"
#include <stdio.h>

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])//added by macfly to use ttl from ipv6 header as hopcount

#include "contikimac-orpl.h"

#if WITH_VARIABLE_TXRATE
static uint16_t compteur=0;
#endif

#define SEND_INTERVAL   (4 * 60 * CLOCK_SECOND)
#define UDP_PORT 1234

static char buf[APP_PAYLOAD_LEN];
static struct simple_udp_connection unicast_connection;

static uint16_t compteur=0;

/*---------------------------------------------------------------------------*/
PROCESS(unicast_sender_process, "ORPL -- Collect-only Application");
AUTOSTART_PROCESSES(&unicast_sender_process);
/*---------------------------------------------------------------------------*/
extern uint16_t dc_obj_metric, dc_obj_count;
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
#if WITH_ORPL_LB && WITH_ORPL_LB_DIO_TARGET
  dc_obj_count+=1;
  dc_obj_metric=(((struct app_data *)data)->dc_metric + (dc_obj_count-1) * dc_obj_metric)/dc_obj_count;
  printf("ORPL_LB: DC metric average %u-%u\n",dc_obj_metric,((struct app_data *)data)->dc_metric);
#endif
  ((struct app_data *)data)->hopcount=uip_ds6_if.cur_hop_limit - UIP_IP_BUF->ttl + 1;//added by macfly to use ttl from ipv6 header as hopcount
  ORPL_LOG_FROM_APPDATAPTR((struct app_data *)data, "App: received");
}
/*---------------------------------------------------------------------------*/
void app_send_to(uint16_t id) {

  static unsigned int cnt;
  struct app_data data;
  uip_ipaddr_t dest_ipaddr;

  data.seqno = ((uint32_t)node_id << 16) + cnt;
  data.src = node_id;
  data.dest = id;
  data.hop = 0;
  data.fpcount = 0;
#if WITH_ORPL_LB & WITH_ORPL_LB_DIO_TARGET
  data.dc_metric=cycle_time* 1000/RTIMER_ARCH_SECOND;
  //data.dc_metric=periodic_tx_dc;
#endif
  //data.wuint = averageWUratio;
  set_ipaddr_from_id(&dest_ipaddr, id);

  data.hopcount=0;//added by macfly to use ttl from ipv6 header as hopcount
  ORPL_LOG_FROM_APPDATAPTR(&data, "App: sending");

  orpl_set_curr_seqno(data.seqno);
  set_ipaddr_from_id(&dest_ipaddr, id);

  *((struct app_data*)buf) = data;
  simple_udp_sendto(&unicast_connection, buf, sizeof(buf) + 1, &dest_ipaddr);

  cnt++;
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_sender_process, ev, data)
{
  static struct etimer periodic_timer;
  static struct etimer send_timer;
  uip_ipaddr_t global_ipaddr;

  PROCESS_BEGIN();

  if(node_id == 0) {
    NETSTACK_RDC.off(0);
    uint16_t mymac = rimeaddr_node_addr.u8[7] << 8 | rimeaddr_node_addr.u8[6];
    printf("Node id unset, my mac is 0x%04x\n", mymac);
    PROCESS_EXIT();
  }

  cc2420_set_txpower(RF_POWER);
  cc2420_set_cca_threshold(RSSI_THR);
#if !WITH_ORPL_LB //the load balancing function included the calcul of the duty cycle
  simple_energest_start();
#endif /*!WITH_ORPL_LB*/
  printf("App: %u starting\n", node_id);
  setLoadBalancing(0);
  deployment_init(&global_ipaddr);
  orpl_init(&global_ipaddr, node_id == ROOT_ID, 1);
  simple_udp_register(&unicast_connection, UDP_PORT,
                      NULL, UDP_PORT, receiver);

  if(node_id == ROOT_ID) {
    NETSTACK_RDC.off(1);
  } else {
    etimer_set(&periodic_timer, 2 * 60 * CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    etimer_set(&periodic_timer, SEND_INTERVAL);
    while(1) {

      etimer_set(&send_timer, random_rand() % (SEND_INTERVAL));
      PROCESS_WAIT_UNTIL(etimer_expired(&send_timer));
      if(orpl_current_edc() != 0xffff) {
        app_send_to(ROOT_ID);
      } else {
        printf("App: not in DODAG\n");
      }
      PROCESS_WAIT_UNTIL(etimer_expired(&periodic_timer));
      etimer_reset(&periodic_timer);
      compteur+=4;
      if(compteur+2>=58 && loadbalancing_is_on==0)//(14*4)=56 and we need to consider the two minutes offset
      {
        setLoadBalancing(1);
        printf("App: LB enabled!\n");
      }
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
