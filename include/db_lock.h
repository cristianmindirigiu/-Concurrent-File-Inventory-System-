#ifndef DB_LOCK_H
#define DB_LOCK_H
#include <sys/types.h>

int apply_lock(int fd, short lock_type, short whence, off_t start, off_t len);
int lock_header_write(int fd);
int unlock_header(int fd);
int lock_record_write(int fd, off_t record_offset, off_t record_size);
int unlock_record(int fd, off_t record_offset, off_t record_size);

#endif
