/*
 * orpl-neighbors.h
 *
 *  Created on: Mar 25, 2014
 *      Author: macfly
 */

#ifndef ORPL_NEIGHBORS_H_
#define ORPL_NEIGHBORS_H_
struct orpl_neighbor {
  struct orpl_neighbor *next;
  uip_ipaddr_t ipaddr;
};



void addNeighbor(uip_ipaddr_t *addr);
void removeNeighbor(uip_ipaddr_t *addr);
struct orpl_neighbor* exist (uip_ipaddr_t *addr);


#endif /* ORPL_NEIGHBORS_H_ */
