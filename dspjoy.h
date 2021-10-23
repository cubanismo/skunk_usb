#ifndef DSPJOY_H_
#define DSPJOY_H_

/*******************************************************************************
 * Joystick button packing format:                                             *
 *                                                                             *
 *   XXXX 369# ot25 80Cs 147* BrRL DUAp                                        *
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
#define JB_C2		12
#define JB_C		13
#define JB_0		14
#define JB_8		15
#define JB_5		16
#define JB_2		17
#define JB_C3		18
#define JB_OPTION	19
#define JB_HASH		20
#define JB_9		21
#define JB_6		22
#define JB_3		23

#define JB_MASK(BUTTON) (1 << JB_##BUTTON)

extern volatile unsigned long butsmem0;
extern volatile unsigned long butsmem1;
extern volatile const unsigned short joyevput;
extern volatile unsigned short joyevget;
extern volatile unsigned short joyevover;
extern volatile unsigned long joyevbuf[];

extern void startdsp(void);
extern void stopdsp(void);

#endif /* DSPJOY_H_ */
