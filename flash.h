#ifndef FLASH_H_
#define FLASH_H_

typedef long (*file_read_proc)(void *priv, char *buf, unsigned int bytes);
extern void flashrom(file_read_proc getdata, void *priv, unsigned short blocks);

#endif /* FLASH_H_ */
