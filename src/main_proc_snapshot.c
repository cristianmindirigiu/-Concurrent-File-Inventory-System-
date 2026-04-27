// programul captureaza procesele curente din /proc/ si scrie intr-o baza de date binara (data/proc.db)


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>


#include "../include/db_format.h"
#include "../include/db_lock.h"

#define MAX_PIDS 32768 //PID-UL maxim implicit pe linux

int is_pid_directory(const char* name); 
int collect_pid_list(pid_t* pids, int max_pids); 
int read_proc_info(pid_t pid, proc_record_t* rec);
void upsert_proc_record(int db_fd, proc_record_t* new_rec);
int open_or_create_db(const char* db_path);
void close_db_contribution(int fd);


//verifica daca numele e doar din cifre (daca e pid)
int is_pid_directory(const char* name){
	if(name == NULL || *name == '\0'){
		return 0;
	}

	for(int i = 0; name[i]!='\0'; i++){
		if(!isdigit((unsigned char)name[i])){
			return 0;
		}
	}
	return 1;
}


//parcurge /proc/ si colecteaza pid-urile intr-un array
//si fixeaza snapshot-ul la momentul apeulului
int collect_pid_list(pid_t* pids, int max_pids){
	DIR* proc_dir = opendir("/proc");

	if(proc_dir == NULL){
		perror("Eroare la deschiderea lui /proc");
		return -1;
	}

	int count = 0;
	struct dirent* entry;

	while((entry = readdir(proc_dir)) != NULL && count < max_pids){
		if(is_pid_directory(entry->d_name)){
			pids[count] = (pid_t)atoi(entry->d_name);
               		count++;
		}
	}
	closedir(proc_dir);
	return count;
}





//citeste info despre un proces din /proc/<pid>/{stat,status,cmdline} si returneaza -1 daca procesul a disparut
int read_proc_info(pid_t pid, proc_record_t* rec){
	char path[256];
	char buffer[4096];
	FILE* fp;

	memset(rec, 0, sizeof(proc_record_t));
	rec->pid = pid;
	
	//citesc din /proc/<pid>/stat

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	fp = fopen(path, "r");
	if(fp == NULL){
		return -1;
	}
	if(fgets(buffer, sizeof(buffer), fp) == NULL){
		fclose(fp);
		return -1;
	}


	//extrag comm-ul (care e intre paranteze)
	
	char* endcomm = strrchr(buffer, ')'); //comm-ul poate avea ) in nume
	if(endcomm == NULL){
		return -1;
	}

	char* startcomm = strchr(buffer, '(');
	if(startcomm == NULL){
		return -1;
	}

	int comm_len = endcomm - startcomm - 1;
	if(comm_len >= MAX_COMM_LEN){
		comm_len = MAX_COMM_LEN - 1;
	}
	memcpy(rec->comm, startcomm + 1, comm_len);
	rec->comm[comm_len] = '\0';

	//sar peste )
	char* p = endcomm + 2;

	pid_t ppid;
	char state;
	unsigned long utime, stime;

	// state(char), ppid(int), 9 campuri ignorate, utime(lu), stime(lu)
	int rez = sscanf(p, "%c %d %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &state, &ppid, 
			&utime, &stime);

	if(rez < 4){
		return -1;
	}

	rec->state = state;
	rec->ppid = ppid;
	rec->cpu_time = utime + stime;


	
	//citesc RSS din /proc/<pid>/status
	
	snprintf(path, sizeof(path), "/proc/%d/status", pid);
	fp = fopen(path, "r");
	if(fp == NULL){
		return -1;
	}


	rec->rss = 0;
	while(fgets(buffer, sizeof(buffer), fp) != NULL){
		if(strncmp(buffer, "VmRSS:", 6) == 0){
			sscanf(buffer + 6, "%lu", &rec->rss);
			break;
		}
	}
	fclose(fp);

	
	//citesc cmdline din /proc/<pid>/cmdline
	
	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	int fd = open(path, O_RDONLY);
	if(fd == -1){
		return -1;
	}

	ssize_t bytes_read = read(fd, rec->cmdline, MAX_CMDLINE_LEN -1);
	close(fd);


	if(bytes_read > 0){
		for(ssize_t i = 0; i < bytes_read - 1 ; i++){
			if(rec->cmdline[i] == '\0'){
				rec->cmdline[i] = ' ';
			}
		}
		rec->cmdline[bytes_read] = '\0';
	} else {
		snprintf(rec->cmdline, MAX_CMDLINE_LEN, "[%s]", rec->comm);
	}
	return 0;
}


// scrie un record in db
// daca pid-ul exita, actualizeaza
// daca nu, adauga un slot nou
// lock pe header pt update count, lock pe record pt scriere
void upsert_proc_record(int db_fd, proc_record_t *new_rec) { 
    db_header_t header;
    off_t record_offset = -1;
    
    lock_header_write(db_fd);
    lseek(db_fd, 0, SEEK_SET);
    read(db_fd, &header, sizeof(db_header_t));
    
    proc_record_t temp_rec;
    for (uint64_t i = 0; i < header.record_count; i++) {
        off_t current_offset = sizeof(db_header_t) + i * sizeof(proc_record_t);
        lseek(db_fd, current_offset, SEEK_SET);
        read(db_fd, &temp_rec, sizeof(proc_record_t));
        
        if (temp_rec.pid == new_rec->pid) {
            record_offset = current_offset;
            break;
        }
    }

    if(record_offset == -1){
	    record_offset = sizeof(db_header_t) + header.record_count * sizeof(proc_record_t);
	    header.record_count++;
	    lseek(db_fd, 0, SEEK_SET);
	    write(db_fd, &header, sizeof(db_header_t));
    }

    unlock_header(db_fd);

    lock_record_write(db_fd, record_offset, sizeof(proc_record_t));
    lseek(db_fd, record_offset, SEEK_SET);
    write(db_fd, new_rec, sizeof(proc_record_t));
    unlock_record(db_fd, record_offset, sizeof(proc_record_t));
}


//deschide db
//daca fisierul e nou, se initializeaza snapshot OPEN
//daca exista OPEN, se ataseaza, iar daca e SEALED da eroare
int open_or_create_db(char const* db_path){
	 int fd = open(db_path, O_RDWR | O_CREAT, 0644);
    	if (fd == -1) {
        	perror("Eroare la deschiderea/crearea bazei de date");
        	return -1;
    	}

    	printf("[PID: %d] Astept lock pe header...\n", getpid());
    	if (lock_header_write(fd) == -1) {
        	close(fd);
        	return -1;
    	}

    	db_header_t header;
    	ssize_t bytes_read = read(fd, &header, sizeof(db_header_t)); 

    	if (bytes_read == 0) {
        	printf("[PID: %d] Initializez baza de date noua...\n", getpid());
        	memcpy(header.magic, MAGIC_PROC, 4);
        	header.format_version = DB_FORMAT_VERSION;
        	header.snapshot_id = (uint64_t)time(NULL); 
        	header.snapshot_state = SNAPSHOT_OPEN;
        	header.active_writers = 1;
        	header.record_count = 0;
    	} else if (bytes_read == sizeof(db_header_t)) {
        	if (strncmp(header.magic, MAGIC_PROC, 4) != 0) {
            		fprintf(stderr, "Eroare: Fisierul nu este un proc.db valid!\n");
            		unlock_header(fd);
            		close(fd);
            		return -1;
        	}

        	if (header.snapshot_state == SNAPSHOT_SEALED) {
            		fprintf(stderr, "Eroare: Snapshot-ul este SEALED. Nu se mai accepta contributii.\n");
            		unlock_header(fd);
            		close(fd);
            	return -1;
        	}

        	header.active_writers++;
        	printf("[PID: %d] M-am atasat la snapshot-ul %llu. Operatori activi: %u\n", 
               	getpid(), (unsigned long long)header.snapshot_id, header.active_writers);
    	} else {
        	fprintf(stderr, "Eroare: Header DB corupt.\n");
        	unlock_header(fd);
        	close(fd);
        	return -1;
    	}

    	lseek(fd, 0, SEEK_SET);
    	write(fd, &header, sizeof(db_header_t));
    	unlock_header(fd);
	return fd;
}



// decrementeaza active_writers iar ultimul writer marcheaza SEALED
void close_db_contribution(int fd){
	db_header_t header;

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

}


// citire arg, deschidere db, colectare pid-uri, scriere recorduri pt fiecare, inchidere
int main(int argc, char* argv[]) {

	char *db_path = "data/proc.db";
	for(int i = 1; i < argc; i++){
		if(strcmp(argv[i], "--db") == 0 && i + 1 < argc){
			db_path = argv[++i];
		}
	}
	int fd = open_or_create_db(db_path);
	if(fd == -1){
		return 1;
	}

	pid_t pids[MAX_PIDS];
	int num_pids = collect_pid_list(pids, MAX_PIDS);

	if(num_pids < 0){
		fprintf(stderr, "Eroare: nu pot citi /proc/\n");
		close(fd);
		return 1;
	}
	printf("[PID: %d] am gasit %d procese in snapshot.\n", getpid(), num_pids);
	for (int i = 0; i < num_pids; i++) {
        	proc_record_t rec;
        	if (read_proc_info(pids[i], &rec) == 0) {
            		upsert_proc_record(fd, &rec);
        	}
    	}
    
    	close_db_contribution(fd);
    	close(fd);
    	return 0;
}








