#ifndef FFSGPU_H_
#define FFSGPU_H_

extern unsigned char fontdata[];

extern void startgpu(void);
extern void stopgpu(void);
extern void clrgamelst(void);
extern void drawstring(const void *fontaddr, const char *stringaddr);

#endif /* FFSGPU_H_ */
