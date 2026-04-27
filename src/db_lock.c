#include "db_lock.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "db_format.h"

int apply_lock(int fd, short lock_type, short whence, off_t start, off_t len) {
    struct flock fl;
    fl.l_type   = lock_type; 
    fl.l_whence = whence;   
    fl.l_start  = start;   
    fl.l_len    = len;      
    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        perror("[Eroare] fcntl a esuat la setarea/ridicarea lacatului");
        return -1;
    }
    return 0;
}
int lock_header_write(int fd) {
    return apply_lock(fd, F_WRLCK, SEEK_SET, 0, sizeof(db_header_t));
}

int unlock_header(int fd) {
    return apply_lock(fd, F_UNLCK, SEEK_SET, 0, sizeof(db_header_t));
}
int lock_record_write(int fd, off_t record_offset, off_t record_size) {
    return apply_lock(fd, F_WRLCK, SEEK_SET, record_offset, record_size);
}
int unlock_record(int fd, off_t record_offset, off_t record_size) {
    return apply_lock(fd, F_UNLCK, SEEK_SET, record_offset, record_size);
}
