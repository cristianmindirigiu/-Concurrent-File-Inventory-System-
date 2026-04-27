#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>

#include "../include/db_format.h"
#include "../include/db_lock.h"

uint32_t calculate_hash(const char *filepath);
void upsert_file_record(int db_fd, file_record_t *new_rec);
void index_dir(const char *current_path, int db_fd);

int main(int argc, char *argv[]) {
    char *root_dir = NULL;
    char *db_path = "data/index.db"; 

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            root_dir = argv[++i];
        } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        }
    }

    if (root_dir == NULL) {
        fprintf(stderr, "Eroare: Argumentul --root este obligatoriu.\n");
        return 1;
    }

    int fd = open(db_path, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        perror("Eroare la deschiderea/crearea bazei de date");
        return 1;
    }

    printf("[PID: %d] Astept lock pe header...\n", getpid());
    if (lock_header_write(fd) == -1) {
        close(fd);
        return 1;
    }

    db_header_t header;
    ssize_t bytes_read = read(fd, &header, sizeof(db_header_t)); 

    if (bytes_read == 0) {
        printf("[PID: %d] Initializez baza de date noua...\n", getpid());
        memcpy(header.magic, MAGIC_INDEX, 4);
        header.format_version = DB_FORMAT_VERSION;
        header.snapshot_id = (uint64_t)time(NULL); 
        header.snapshot_state = SNAPSHOT_OPEN;
        header.active_writers = 1;
        header.record_count = 0;
    } else if (bytes_read == sizeof(db_header_t)) {
        if (strncmp(header.magic, MAGIC_INDEX, 4) != 0) {
            fprintf(stderr, "Eroare: Fisierul nu este un index.db valid!\n");
            unlock_header(fd);
            close(fd);
            return 1;
        }

        if (header.snapshot_state == SNAPSHOT_SEALED) {
            fprintf(stderr, "Eroare: Snapshot-ul este SEALED. Nu se mai accepta contributii.\n");
            unlock_header(fd);
            close(fd);
            return 1;
        }

        header.active_writers++;
        printf("[PID: %d] M-am atasat la snapshot-ul %llu. Operatori activi: %u\n", 
               getpid(), (unsigned long long)header.snapshot_id, header.active_writers);
    } else {
        fprintf(stderr, "Eroare: Header DB corupt.\n");
        unlock_header(fd);
        close(fd);
        return 1;
    }

    lseek(fd, 0, SEEK_SET);
    write(fd, &header, sizeof(db_header_t));
    unlock_header(fd);

    printf("[PID: %d] Incep indexarea din %s...\n", getpid(), root_dir);
    
    index_dir(root_dir, fd); 

    lock_header_write(fd);
    lseek(fd, 0, SEEK_SET);
    read(fd, &header, sizeof(db_header_t));

    header.active_writers--;
    
    if (header.active_writers == 0) {
        header.snapshot_state = SNAPSHOT_SEALED;
        printf("[PID: %d] Sunt ultimul writer. Marchez snapshot-ul ca SEALED.\n", getpid());
    } else {
        printf("[PID: %d] Iesire din snapshot. Au mai ramas %u operatori.\n", getpid(), header.active_writers);
    }

    lseek(fd, 0, SEEK_SET);
    write(fd, &header, sizeof(db_header_t));
    unlock_header(fd);

    close(fd);
    
    return 0;
}


uint32_t calculate_hash(const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) return 0;

    uint32_t hash = 5381;
    unsigned char buffer[4096]; 
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            hash = ((hash << 5) + hash) + buffer[i]; 
        }
    }
    close(fd);
    return hash;
}
 
void upsert_file_record(int db_fd, file_record_t *new_rec) {
    db_header_t header;
    off_t record_offset = -1;
    
    lock_header_write(db_fd);
    lseek(db_fd, 0, SEEK_SET);
    read(db_fd, &header, sizeof(db_header_t));
    
    file_record_t temp_rec;
    for (uint64_t i = 0; i < header.record_count; i++) {
        off_t current_offset = sizeof(db_header_t) + i * sizeof(file_record_t);
        lseek(db_fd, current_offset, SEEK_SET);
        read(db_fd, &temp_rec, sizeof(file_record_t));
        
        if (strcmp(temp_rec.path, new_rec->path) == 0) {
            record_offset = current_offset;
            break;
        }
    }
    
     
    if (record_offset == -1) {
        record_offset = sizeof(db_header_t) + header.record_count * sizeof(file_record_t);
        header.record_count++;
        lseek(db_fd, 0, SEEK_SET);
        write(db_fd, &header, sizeof(db_header_t));
    }
    
    unlock_header(db_fd);

    lock_record_write(db_fd, record_offset, sizeof(file_record_t));
    lseek(db_fd, record_offset, SEEK_SET);
    write(db_fd, new_rec, sizeof(file_record_t));
    unlock_record(db_fd, record_offset, sizeof(file_record_t));
}

void index_dir(const char *current_path, int db_fd) {
    DIR *dir = opendir(current_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", current_path, entry->d_name);

        struct stat st;
        if (lstat(full_path, &st) == -1) continue;

        file_record_t rec;
        memset(&rec, 0, sizeof(file_record_t));
        strncpy(rec.path, full_path, MAX_PATH_LEN - 1);
        rec.st_dev = st.st_dev;
        rec.st_ino = st.st_ino;
        rec.mtime = st.st_mtime;
        
        if (S_ISREG(st.st_mode)) {
            rec.type = 0; 
            rec.size = st.st_size;
            rec.hash = calculate_hash(full_path);
        } else if (S_ISDIR(st.st_mode)) {
            rec.type = 1; 
            rec.size = 0;
            index_dir(full_path, db_fd);
        } else if (S_ISLNK(st.st_mode)) {
            rec.type = 2; 
            rec.size = 0;
           
            readlink(full_path, rec.symlink_target, MAX_PATH_LEN - 1);
        } else if (S_ISFIFO(st.st_mode)) {
            rec.type = 3; 
            rec.size = 0;
        } else {
            rec.type = 4; 
            rec.size = 0;
        }

        upsert_file_record(db_fd, &rec);
    }
    closedir(dir);
}
