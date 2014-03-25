#include "contiki-conf.h"
#include <string.h>
#include "orpl.h"
#include "orpl-neighbors.h"
#include "net/uip.h"
#include "lib/memb.h"
#include "lib/list.h"

/**
 * hack to avoid any-to-any bug which makes a node sending the ACK too late wheen looking if neighbor is reachable (modif in orpl-anycast.c)
 */

struct orpl_neighbor* head=NULL;

MEMB(neighbor_orpl_mem, struct orpl_neighbor, 20);

LIST(orpl_neighbors);

void addNeighbor(uip_ipaddr_t *addr){
//
//  printf("Nbr : Add : ");
//  uip_debug_ipaddr_print(&addr);
//  printf("\n");
  struct orpl_neighbor* nbr=memb_alloc(&neighbor_orpl_mem);
  nbr->ipaddr=*addr;
  list_push(orpl_neighbors, nbr);
}

int removeNeighbor(uip_ipaddr_t *addr){
  struct orpl_neighbor *item = exist(addr);

  if(item != NULL) {
    list_remove(orpl_neighbors, item);
    memb_free(&neighbor_orpl_mem, item);
    return 0;
  }
  return -1;
}

struct orpl_neighbor* exist(uip_ipaddr_t *addr){
  struct orpl_neighbor *item;
  item = list_head(orpl_neighbors);
  while(item != NULL) {
    if(uip_ipaddr_cmp(&item->ipaddr, addr)) {
      return item;
    }
    item = item->next;
  }
  return NULL;
}
