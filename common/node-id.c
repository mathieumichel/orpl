/*
 * Copyright (c) 2006, Swedish Institute of Computer Science.
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
 *         Utility to store a node id in the external flash
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include "deployment.h"
#include "sys/node-id.h"
#include "net/uip.h"
#include "contiki-conf.h"
#include "dev/xmem.h"
#include "net/rime/rimeaddr.h"
#include "net/uip-ds6.h"
#include "random.h"

unsigned short node_id = 0;

struct id_mac {
  uint16_t id;
  uint16_t mac;
};

static const uint16_t any_to_any_list[] = {
#if IN_INDRIYA
    1, 17, 28, 50, 56, 74, 121, 124, 126,
#elif IN_COOJA
  1, 2, 4, 6, 8,
#endif
  0
};

static const struct id_mac id_mac_list[] = {
#if IN_TWIST
    {202, 0xfd30}, {187, 0xb32b}, {198,0xc823}, {199, 0x9b17}, {13, 0xf600}, {241, 0x3f0a}, {102, 0xc033}, {137, 0xfab2}, {190, 0x4b28}, {148, 0x35d9}, {95, 0x3e4e}, {93, 0xaa65}, {145, 0x97b1}, {139, 0x13a3}, {15, 0x2443}, {87, 0xc6ff}, {90, 0x167b}, {10, 0x0b63}, {82, 0x0649}, {262, 0x29a6}, {220, 0xa53c}, {100, 0x2058}, {228, 0xcc2b}, {230, 0x5a12}, {252, 0x94da}, {142, 0xf0a9}, {141, 0x1ec1}, {224, 0x5331}, {92, 0x9649}, {99, 0xafe6}, {222, 0x3311}, {205, 0xdf22}, {223, 0xfa35}, {189, 0x8622}, {138, 0xe1fc}, {83, 0x68c6}, {143, 0x34ab}, {221, 0x3c1a}, {80, 0x6fb0}, {195, 0x752a}, {153, 0x99cf}, {231, 0x63b1}, {103, 0x1ff4}, {250, 0x1ea9}, {212, 0xf939}, {211, 0x1812}, {12, 0xc7ee}, {101, 0x8268}, {185, 0xb0ad}, {11, 0x3c5b}, {272, 0x6671}, {208, 0x9733}, {88, 0x1100}, {152, 0xe8d4}, {97, 0x8165}, {186, 0x5914}, {214, 0xd621}, {144, 0xe29f}, {192, 0x9135}, {197, 0x93ae}, {200, 0x7410}, {218, 0x6d2d}, {96, 0x0e64}, {79, 0x825e}, {251, 0xc5a5}, {81, 0x3e5b}, {89, 0xf861}, {149, 0xc0b9}, {206, 0x9c2e}, {146, 0x2295}, {225, 0x5d32}, {207, 0x3b23}, {229, 0x8528}, {204, 0x8212}, {151, 0xf1c8}, {203, 0xd91c}, {213, 0x8f10}, {191, 0x4739}, {147, 0xb8e4}, {240, 0x68fa}, {140, 0x17f1}, {196, 0x5128}, {216, 0x3b16}, {150, 0xe987}, {209, 0x491a}, {249, 0x8c2e}, {84, 0x64ec}, {91, 0x796f}, {94, 0xc967}, {194, 0xe13d}, {154, 0xd782}, {85, 0x593a}, {86, 0x6903}, {215, 0x1b1f},
//    {202, 0xfd30}, {187, 0xb32b}, {198,0xc823}, {199, 0x9b17}, {13, 0xf600}, {241, 0x3f0a}, {102, 0xc033}, {137, 0xfab2}, {190, 0x4b28}, {148, 0x35d9}, {95, 0x3e4e}, {93, 0xaa65}, {145, 0x97b1}, {139, 0x13a3}, {15, 0x2443}, {87, 0xc6ff}, {90, 0x167b}, {10, 0x0b63}, {82, 0x0649}, {262, 0x29a6}, {220, 0xa53c}, {100, 0x2058}, {228, 0xcc2b}, {230, 0x5a12}, {252, 0x94da}, {142, 0xf0a9}, {141, 0x1ec1}, {92, 0x9649}, {99, 0xafe6}, {222, 0x3311}, {205, 0xdf22}, {223, 0xfa35}, {189, 0x8622}, {138, 0xe1fc}, {83, 0x68c6}, {143, 0x34ab}, {221, 0x3c1a}, {80, 0x6fb0}, {195, 0x752a}, {153, 0x99cf}, {231, 0x63b1}, {103, 0x1ff4}, {250, 0x1ea9}, {212, 0xf939}, {211, 0x1812}, {12, 0xc7ee}, {101, 0x8268}, {185, 0xb0ad}, {11, 0x3c5b}, {208, 0x9733}, {88, 0x1100}, {152, 0xe8d4}, {186, 0x5914}, {214, 0xd621}, {144, 0xe29f}, {192, 0x9135}, {197, 0x93ae}, {200, 0x7410}, {218, 0x6d2d}, {96, 0x0e64}, {79, 0x825e}, {251, 0xc5a5}, {81, 0x3e5b}, {89, 0xf861}, {149, 0xc0b9}, {146, 0x2295}, {225, 0x5d32}, {229, 0x8528}, {204, 0x8212}, {151, 0xf1c8}, {203, 0xd91c}, {213, 0x8f10}, {191, 0x4739}, {147, 0xb8e4}, {240, 0x68fa}, {140, 0x17f1}, {196, 0x5128}, {216, 0x3b16}, {150, 0xe987}, {249, 0x8c2e}, {84, 0x64ec}, {91, 0x796f}, {94, 0xc967}, {194, 0xe13d}, {154, 0xd782}, {85, 0x593a}, {86, 0x6903}, {215, 0x1b1f},
#elif IN_INDRIYA
    {1, 0xaeb3}, {2, 0x7e40}, {3, 0x11ed}, {4, 0xf3db}, {5, 0x3472}, {6, 0x16b9}, {7, 0x9887}, {8, 0x6558}, {9, 0x655f}, {10, 0xf756}, {11, 0x7677}, {12, 0xa699}, {13, 0x1b99}, {14, 0x4117}, {15, 0xd86a}, {16, 0x9188}, {17, 0xe611}, {18, 0x1160}, {19, 0x2190}, {20, 0x0041}, {21, 0xb6cc}, {22, 0x10c5}, {24, 0x14cc}, {25, 0x4a3f}, {26, 0x3fac}, {27, 0xf49d}, {28, 0xb2d8}, {30, 0xc07d}, {31, 0x0d5f}, {32, 0xb0a3}, {33, 0xb5d8}, {34, 0x5156}, {35, 0x63b0}, {36, 0x260c}, {37, 0x9586}, {38, 0x1b21}, {39, 0x7e48}, {40, 0x2af3}, {41, 0x98e2}, {42, 0x0eee}, {43, 0x750f}, {44, 0x5da1}, {45, 0x0856}, {46, 0x4e4c}, {47, 0x8f78}, {48, 0x2f0b}, {50, 0xa9c4}, {51, 0xfa5b}, {52, 0x65c2}, {53, 0x83cd}, {54, 0xd634}, {55, 0x4d21}, {56, 0x61b4}, {57, 0xdc77}, {58, 0xd393}, {60, 0xcd5d}, {63, 0x362a}, {64, 0x5916}, {65, 0xa24e}, {66, 0x701c}, {68, 0x8b87}, {69, 0x3ed9}, {70, 0xe771}, {71, 0x261c}, {72, 0xc945}, {73, 0xb245}, {74, 0x3e01}, {75, 0xa95c}, {76, 0xac09}, {77, 0x6d78}, {78, 0xfa5c}, {79, 0xb8c3}, {80, 0xf58a}, {81, 0xe804}, {82, 0xbffd}, {83, 0x2edd}, {84, 0xc87d}, {85, 0x8c75}, {115, 0x9bb0}, {116, 0x56f2}, {117, 0x40d1}, {118, 0xbde5}, {119, 0xb13b}, {120, 0xc5d3}, {121, 0xb54e}, {122, 0x7713}, {123, 0x9ef9}, {124, 0x82cd}, {126, 0xd9f6}, {127, 0x4eab}, {128, 0xdc44}, {129, 0x0a03}, {130, 0xabd9}, {131, 0x7811}, {132, 0x6ec0}, {133, 0x36ee}, {134, 0xea27}, {135, 0x7aed}, {136, 0x57f3}, {137, 0x2def}, {138, 0xc9f5}, {139, 0x148d},
    //{1, 0xaeb3}, {2, 0x11ed}, {3, 0xf3db}, {4, 0x7e40}, {5, 0x16b9}, {6, 0x3472}, {7, 0x9887}, {8, 0x6558}, {9, 0x655f}, {10, 0x7677}, {11, 0xa699}, {12, 0x1b99}, {13, 0x4117}, {14, 0xf756}, {15, 0xd86a}, {16, 0x9188}, {17, 0xe611}, {18, 0xb6cc}, {19, 0x1160}, {20, 0x2190}, {21, 0x0041}, {22, 0x10c5}, {23, 0x17a8}, {24, 0x14cc}, {25, 0x4a3f}, {26, 0x5fdc}, {27, 0x3fac}, {28, 0xf49d}, {29, 0xb2d8}, {30, 0x0d5f}, {31, 0xc07d}, {32, 0xb0a3}, {33, 0x5156}, {34, 0xb5d8}, {35, 0x63b0}, {36, 0x260c}, {37, 0x9586}, {38, 0x1b21}, {39, 0x7e48}, {40, 0x98e2}, {41, 0x9161}, {42, 0x53db}, {43, 0x9959}, {44, 0x5da1}, {45, 0x843a}, {46, 0x8f78}, {47, 0xf7cf}, {48, 0x0fbd}, {49, 0x58b6}, {50, 0xfa5b}, {51, 0xa9c4}, {52, 0x65c2}, {53, 0x83cd}, {54, 0x190a}, {55, 0x3a54}, {56, 0xb245}, {57, 0x61b4}, {58, 0xdc77}, {59, 0xd393}, {60, 0x916b}, {61, 0xd634}, {62, 0x8e51}, {63, 0x362a}, {64, 0x5916}, {65, 0x0083}, {66, 0xea46}, {67, 0x5396}, {68, 0x701c}, {69, 0x8b87}, {70, 0xbb9c}, {71, 0xe771}, {72, 0x01b6}, {73, 0x2e77}, {74, 0xa982}, {75, 0xc75d}, {76, 0x627e}, {77, 0xac09}, {78, 0x6d78}, {79, 0xfa5c}, {80, 0xb8c3}, {81, 0xf58a}, {82, 0x0840}, {83, 0xd098}, {84, 0x6c98}, {85, 0xc87d}, {86, 0x8c75}, {87, 0x9ab0}, {88, 0x0c6b}, {89, 0x946d}, {90, 0x87f5}, {91, 0x7902}, {92, 0x8b56}, {93, 0x7046}, {94, 0xfccf}, {95, 0x21b5}, {96, 0xc5d3}, {97, 0x3a8a}, {98, 0x7b79}, {99, 0xc03b}, {100, 0xad7e}, {101, 0xdc47}, {102, 0xed79}, {103, 0x3c95}, {104, 0x7f91}, {105, 0xae62}, {106, 0x8874}, {107, 0x7bab}, {108, 0xf960}, {110, 0x5f59}, {111, 0x479d}, {112, 0x7b44}, {113, 0xf460}, {114, 0x75b8}, {115, 0x9bb0}, {116, 0xc698}, {117, 0x40d1}, {118, 0xbde5}, {119, 0xb13b}, {120, 0x4fc1}, {121, 0xb54e}, {122, 0x6c18}, {123, 0xb880}, {124, 0x82cd}, {125, 0x774e}, {126, 0xd9f6}, {127, 0x4eab}, {128, 0xdc44}, {129, 0x0653}, {130, 0xdd97}, {131, 0xabd9}, {132, 0xf968}, {133, 0xea27}, {134, 0x4e82}, {135, 0x2997}, {136, 0xea49}, {137, 0xfd79}, {138, 0x38e7}, {139, 0x148d},
    //{1, 0xaeb3}, {2, 0x11ed}, {3, 0xf3db}, {4, 0x7e40}, {5, 0x16b9}, {6, 0x3472}, {7, 0x9887}, {8, 0x6558}, {9, 0x655f}, {10, 0x7677}, {11, 0xa699}, {12, 0x1b99}, {13, 0x4117}, {14, 0xf756}, {15, 0xd86a}, {16, 0x9188}, {17, 0xe611}, {18, 0xb6cc}, {19, 0x1160}, {20, 0x2190}, {22, 0x10c5}, {23, 0x17a8}, {24, 0x14cc}, {25, 0x4a3f}, {26, 0x5fdc}, {27, 0x3fac}, {28, 0xf49d}, {29, 0xb2d8}, {30, 0x0d5f}, {31, 0xc07d}, {32, 0xb0a3}, {33, 0x5156}, {34, 0xb5d8}, {35, 0x63b0}, {36, 0x260c}, {37, 0x9586}, {38, 0x1b21}, {39, 0x7e48}, {40, 0x98e2}, {41, 0x9161}, {42, 0x53db}, {43, 0x9959}, {44, 0x5da1}, {46, 0x8f78}, {47, 0xf7cf}, {49, 0x58b6}, {50, 0xfa5b}, {51, 0xa9c4}, {52, 0x65c2}, {53, 0x83cd}, {54, 0x190a}, {55, 0x3a54}, {56, 0xb245}, {57, 0x61b4}, {58, 0xdc77}, {59, 0xd393}, {60, 0x916b}, {61, 0xd634}, {62, 0x8e51}, {63, 0x362a}, {64, 0x5916}, {65, 0x0083}, {66, 0xea46}, {67, 0x5396}, {68, 0x701c}, {69, 0x8b87}, {70, 0xbb9c}, {71, 0xe771}, {72, 0x01b6}, {73, 0x2e77}, {74, 0xa982}, {75, 0xc75d}, {76, 0x627e}, {77, 0xac09}, {78, 0x6d78}, {79, 0xfa5c}, {80, 0xb8c3}, {81, 0xf58a}, {82, 0x0840}, {83, 0xd098}, {84, 0x6c98}, {85, 0xc87d}, {86, 0x8c75}, {87, 0x9ab0}, {88, 0x0c6b}, {89, 0x946d}, {90, 0x87f5}, {91, 0x7902}, {92, 0x8b56}, {93, 0x7046}, {94, 0xfccf}, {95, 0x21b5}, {96, 0xc5d3}, {97, 0x3a8a}, {98, 0x7b79}, {99, 0xc03b}, {100, 0xad7e}, {101, 0xdc47}, {102, 0xed79}, {103, 0x3c95}, {104, 0x7f91}, {105, 0xae62}, {106, 0x8874}, {107, 0x7bab}, {108, 0xf960}, {110, 0x5f59}, {111, 0x479d}, {112, 0x7b44}, {113, 0xf460}, {114, 0x75b8}, {115, 0x9bb0}, {116, 0xc698}, {117, 0x40d1}, {118, 0xbde5}, {119, 0xb13b}, {120, 0x4fc1}, {121, 0xb54e}, {122, 0x6c18}, {123, 0xb880}, {124, 0x82cd}, {125, 0x774e}, {126, 0xd9f6}, {127, 0x4eab}, {128, 0xdc44}, {129, 0x0653}, {130, 0xdd97}, {131, 0xabd9}, {132, 0xf968}, {133, 0xea27}, {134, 0x4e82}, {135, 0x2997}, {136, 0xea49}, {137, 0xfd79}, {138, 0x38e7}, {139, 0x148d},
#elif IN_MOTES
    {1, 0x111f}, {2, 0x180b}, {3, 0x44b3},
#endif
    {0, 0x0000}
};

#if IN_COOJA
#define N_NODES 8
#else
#define N_NODES ((sizeof(id_mac_list)/sizeof(struct id_mac))-1)
#endif

#if (IN_TWIST || IN_INDRIYA || IN_MOTES)

uint16_t node_id_from_rimeaddr(const rimeaddr_t *addr) {
  if(addr == NULL) return 0;
  uint16_t mymac = addr->u8[7] << 8 | addr->u8[6];
  const struct id_mac *curr = id_mac_list;
  while(curr->mac != 0) {
    if(curr->mac == mymac) {
      return curr->id;
    }
    curr++;
  }
  return 0;
}

#else /* (IN_TWIST || IN_INDRIYA || IN_MOTES) */

uint16_t node_id_from_rimeaddr(const rimeaddr_t *addr) {
  if(addr == NULL) return 0;
  else return addr->u8[7];
}

#endif /* (IN_TWIST || IN_INDRIYA || IN_MOTES) */

uint16_t node_id_from_lipaddr(const uip_ipaddr_t *addr) {
  return (addr->u8[14] << 8) + addr->u8[15];
//  uip_ds6_nbr_t *nbr = uip_ds6_nbr_lookup(addr);
//  if(nbr) return node_id_from_rimeaddr(&nbr->lladdr);
//  else return 0;
}

uint16_t node_id_from_ipaddr(const uip_ipaddr_t *addr) {
  return (addr->u8[14] << 8) + addr->u8[15];
}

void node_id_restore() {
  printf("node_id_restore %u %u\n", node_id, node_id_from_rimeaddr(&rimeaddr_node_addr));
  node_id = node_id_from_rimeaddr(&rimeaddr_node_addr);
}

uint16_t get_id(uint16_t index) {
#if IN_COOJA
  return index;
#else
  uint16_t id_count = (sizeof(id_mac_list)/sizeof(struct id_mac))-1;
  return id_mac_list[index % id_count].id;
#endif
}

uint16_t get_n_nodes() {
  return N_NODES;
}

uint16_t get_node_id(uint16_t index) {
#if IN_COOJA
  return 1 + (index % N_NODES);
#else
  return id_mac_list[index % N_NODES].id;
#endif
}

uint16_t get_random_id() {
#if IN_COOJA
  return 1 + ((uint16_t)random_rand()) % N_NODES;
#else
  uint16_t id_count = (sizeof(id_mac_list)/sizeof(struct id_mac))-1;
  return id_mac_list[((uint16_t)random_rand()) % id_count].id;
#endif
}

int id_has_outage(uint16_t id) {
  return id % 4 == 1;
}

int has_outage(void) {
  return id_has_outage(node_id);
}

int is_id_addressable(uint16_t id) {
#if (ALL_NODES_ADDRESSABLE == 0)
  return id % 2 == 0;
#else
  return 1;
#endif
}

int is_addressable() {
  return is_id_addressable(node_id);
}

int is_id_in_any_to_any(uint16_t id) {
  const uint16_t *curr = any_to_any_list;
  while(*curr != 0) {
    if(*curr == id) {
      return 1;
    }
    curr++;
  }
  return 0;
}

int is_in_any_to_any() {
  return is_id_in_any_to_any(node_id);
}
