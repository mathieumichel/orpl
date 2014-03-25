#include "contiki-conf.h"
#include <string.h>
#include "orpl.h"
#include "orpl-neighbors.h"
#include "net/uip.h"

static struct orpl_neighbor* head=NULL;



void addNeighbor(uip_ipaddr_t *addr){
  struct orpl_neighbor nbr;
  nbr->ipaddr=addr;
  nbr->next=head;
  head=nbr;
}

int removeNeighbor(uip_ipaddr_t *addr){
  struct orpl_neighbor *temp=head;
  if(uip_ipaddr_cmp(&temp->ipaddr, addr))
  {
    return 1;
  }
  while(temp->next!=NULL){
    if(uip_ipaddr_cmp(&temp->next->ipaddr, addr)){
      temp->next=&temp->next->next;
      return 1;
    }
    temp=&(&temp->next)->ipaddr;
  }
  return 0;
}

int exist(uip_ipaddr_t *addr){
  struct orpl_neighbor *temp=head;
  while(temp!=NULL){
    if(uip_ipaddr_cmp(&temp->ipaddr, addr)){
      return 1;
    }
    temp=temp->next;
  }
  return 0;
}
