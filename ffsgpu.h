#ifndef FFSGPU_H_
#define FFSGPU_H_

#define GL_WIDTH		192
#define FNTHEIGHT		12

extern unsigned char gamelstbm[];

extern void startgpu(void);
extern void stopgpu(void);
extern void clrgamelst(void);
extern void drawstring(const void *surfaddr,
					   unsigned coords,
					   const char *stringaddr);
extern void invertrect(const void *surfaddr,
					   unsigned coords,
					   unsigned size);

#endif /* FFSGPU_H_ */
