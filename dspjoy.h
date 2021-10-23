#ifndef DSPJOY_H_
#define DSPJOY_H_

/*******************************************************************************
 * Joystick button packing format:                                             *
 *                                                                             *
 *   XXXX ot36 9#Cs 2580 147* BrRL DUAp                                        *
 *                                                                             *
 * See PARSEBUTNS macro description in dspjoy.s for more details.              *
 ******************************************************************************/

#define JB_PAUSE	0
#define JB_A		1
#define JB_UP		2
#define JB_DOWN		3
#define JB_LEFT		4
#define JB_RIGHT	5
#define JB_C1		6
#define JB_B		7
#define JB_STAR		8
#define JB_7		9
#define JB_4		10
#define JB_1		11
#define JB_0		12
#define JB_8		13
#define JB_5		14
#define JB_2		15
#define JB_C2		16
#define JB_C		17
#define JB_HASH		18
#define JB_9		19
#define JB_6		20
#define JB_3		21
#define JB_C3		22
#define JB_OPTION	23

#define JB_MASK(BUTTON) (1 << JB_##BUTTON)

extern volatile unsigned long butsmem0;
extern volatile unsigned long butsmem1;

extern void startdsp(void);
extern void stopdsp(void);

#endif /* DSPJOY_H_ */
