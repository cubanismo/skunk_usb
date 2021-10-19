#ifndef FFSGPU_H_
#define FFSGPU_H_

extern unsigned char gamelstbm[];

extern void startgpu(void);
extern void stopgpu(void);
extern void clrgamelst(void);
extern void drawstring(const void *surfaddr,
                       unsigned coords,
                       const char *stringaddr);

#endif /* FFSGPU_H_ */
