#ifndef DB_FORMAT_H
#define DB_FORMAT_H

#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define MAGIC_INDEX "IDXI" 
#define MAGIC_PROC  "PRCI" 

#define DB_FORMAT_VERSION 1 
#define SNAPSHOT_OPEN   0 
#define SNAPSHOT_SEALED 1


#define MAX_PATH_LEN    1024
#define MAX_COMM_LEN    256
#define MAX_CMDLINE_LEN 1024

typedef struct {
    char magic[4];             
    uint32_t format_version;   
    uint64_t snapshot_id;      
    uint8_t snapshot_state;    
    uint32_t active_writers;  
    uint64_t record_count;   
} db_header_t;


typedef struct {
    char path[MAX_PATH_LEN];          
    uint8_t type;                    
    uint64_t size;                    
    time_t mtime;                     
    uint64_t st_dev;                  
    uint64_t st_ino;                 
    uint32_t hash;        
    char symlink_target[MAX_PATH_LEN];
} file_record_t;


typedef struct {
    pid_t pid;                       
    pid_t ppid;                      
    char state;                       
    char comm[MAX_COMM_LEN];          
    char cmdline[MAX_CMDLINE_LEN];   
    uint64_t rss;                     
    uint64_t cpu_time;              
} proc_record_t;

#endif 
