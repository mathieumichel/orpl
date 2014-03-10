/*
 * contikimac-orpl.h
 *
 *  Created on: Feb 11, 2014
 *      Author: macfly
 */

#ifndef CONTIKIMAC_ORPL_H_
#define CONTIKIMAC_ORPL_H_

extern uint32_t cycle_time;
extern uint16_t periodic_tx_dc;
extern uint16_t periodic_dc;
#if WITH_ORPL_LB
extern int loadbalancing_is_on;
#endif /*WITH_ORPL_LB*/

#endif /* CONTIKIMAC_ORPL_H_ */
