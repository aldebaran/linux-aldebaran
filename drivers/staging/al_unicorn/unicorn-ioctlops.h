#ifndef UNICORN_IOCTLOPS_H
#define UNICORN_IOCTLOPS_H

void res_free(struct unicorn_dev *dev, struct unicorn_fh *fh, unsigned int bits);
int res_locked(struct unicorn_dev *dev, unsigned int bit);
int res_check(struct unicorn_fh *fh, unsigned int bit);
#endif
