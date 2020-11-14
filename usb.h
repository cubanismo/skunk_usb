#ifndef _USB_H_
#define _USB_H_

extern void initbulkdev(short port);
extern void readblock(short port, int blocknum, char *outBuf);

#endif /* _USB_H_ */
