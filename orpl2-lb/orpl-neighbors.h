/*
 * orpl-neighbors.h
 *
 *  Created on: Mar 25, 2014
 *      Author: macfly
 */

#ifndef ORPL_NEIGHBORS_H_
#define ORPL_NEIGHBORS_H_
struct orpl_neighbor {
  uip_ipaddr_t ipaddr;
  struct orpl_neighbor *next;
};
void addNeighbor(uip_ipaddr_t *addr);
int removeNeighbor(uip_ipaddr_t *addr);
int exist(uip_ipaddr_t *addr);

#endif /* ORPL_NEIGHBORS_H_ */
