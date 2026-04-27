//utilitar pt a compara doua baze de date binare
//rulare : ./bin/db_diff --old <db1> --new <db2> --out <raport>
//produce raport text cu aparute/disparute/modificate
//lucreaza si pe IDXI si pe PRCI



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "../include/db_format.h"



int verifica_compatibilitate(db_header_t* h_old, db_header_t* h_new);
void diff_files(file_record_t* old_recs, uint64_t old_count, file_record_t* new_recs, uint64_t new_count, FILE* out);
void diff_proc(proc_record_t* old_recs, uint64_t old_count, proc_record_t* new_recs, uint64_t new_count, FILE* out);
int gaseste_index_path(file_record_t* array, uint64_t count, const char* path);
int gaseste_index_pid(proc_record_t* array, uint64_t count, pid_t pid);

//verifica daca cele 2 headere au acelasi magic si format_version
int verifica_compatibilitate(db_header_t* h_old, db_header_t* h_new){
	if(memcmp(h_old->magic, h_new->magic, 4) == 0  && h_old->format_version == h_new->format_version){
		return 0;
	} else {
		return -1;
	}
}

//cauta liniar un path intr-un array de file_record_t; returneaza indexul gasit sau -1 daca nu exista
int gaseste_index_path(file_record_t* array, uint64_t count, const char* path){
	for(int i = 0; i < count; i++){
		if(strcmp(array[i].path, path) == 0){
			return i;
		}
	}
	return -1;
}

//la fel doar ca pt proc
int gaseste_index_pid(proc_record_t* array, uint64_t count, pid_t pid){
	for(int i = 0; i < count; i++){
		if(array[i].pid == pid){
			return i;
		}
	}
	return -1;
}

//compara 2 snapshot-uri de tip index.db si scrie raportul, care are 3 sectiuni (aparute, disparute, modificate)
//aparute (pathuri din new care nu exista in old)
//disparute (pathuri din old care nu exista in new)
//modificate(pathuri in ambele, dar cu macar un camp diferit - type size ..)
void diff_files(file_record_t* old_recs, uint64_t old_count, file_record_t* new_recs, uint64_t new_count, FILE* out){
	fprintf(out, "===APARUTE===\n");
	for(uint64_t i = 0; i < new_count; i++){
		if(gaseste_index_path(old_recs, old_count, new_recs[i].path) == -1){
			fprintf(out, "%s\n", new_recs[i].path); 
			
		}
	}
	fprintf(out, "===DISPARUTE===\n");
	for(uint64_t i = 0; i < old_count; i++){
		if(gaseste_index_path(new_recs, new_count, old_recs[i].path) == -1){
			fprintf(out, "%s\n", old_recs[i].path);
		}
	}
	fprintf(out, "===MODIFICATE===\n");
	for(uint64_t i = 0; i < new_count; i++){
		int idx = gaseste_index_path(old_recs, old_count, new_recs[i].path);
		if (idx >= 0){
			file_record_t* old_r = &old_recs[idx];
			file_record_t* new_r = &new_recs[i];

			if(old_r->type != new_r->type || 
				old_r->size != new_r->size ||
				old_r->mtime != new_r->mtime ||
				old_r->hash != new_r->hash ||
				strcmp(old_r->symlink_target, new_r->symlink_target) != 0) {
					fprintf(out, "%s\n", new_recs[i].path);
					if(old_r->type != new_r->type){
						fprintf(out, " type: %u -> %u\n", old_r->type, new_r->type);
					}
					if(old_r->size != new_r->size){
						fprintf(out, " size: %lu -> %lu\n", (unsigned long)old_r->size, (unsigned long)new_r->size);
					}
					if(old_r->mtime != new_r->mtime){
						fprintf(out, " mtime modificat\n");
					}
					if(old_r->hash != new_r->hash){
						fprintf(out, " hash: 0x%x -> 0x%x\n", old_r->hash, new_r->hash);
					}
					if(strcmp(old_r->symlink_target, new_r->symlink_target) != 0){
						fprintf(out, " symlink: %s -> %s\n", old_r->symlink_target, new_r->symlink_target);
					}

				}
		}
	}
}

//compara 2 snapshot-uri de tip proc.db
//aparute : pid-uri noi
//disparute : pid-uri care nu mai exista
//modificate : pid-uri existente cu schimbari semnificative
void diff_proc(proc_record_t* old_recs, uint64_t old_count , proc_record_t* new_recs, uint64_t new_count, FILE* out){
	fprintf(out, "===APARUTE===\n");
	for(uint64_t i = 0; i < new_count; i++){
		if(gaseste_index_pid(old_recs, old_count, new_recs[i].pid) == -1){
			fprintf(out, "PID %d (%s)\n", new_recs[i].pid, new_recs[i].comm);
		}
	}
	fprintf(out, "===DISPARUTE===\n");
	for(uint64_t i = 0; i < old_count; i++){
		if(gaseste_index_pid(new_recs, new_count, old_recs[i].pid) == -1){
			fprintf(out, "PID %d (%s)\n", old_recs[i].pid, old_recs[i].comm);
		}
	}
	fprintf(out, "===MODIFICATE===\n");
	for(uint64_t i = 0; i < new_count; i++){
		int idx = gaseste_index_pid(old_recs, old_count, new_recs[i].pid);
		if(idx>=0){
			proc_record_t* old_r = &old_recs[idx];
			proc_record_t* new_r = &new_recs[i];

			//calc diferenta abs de RSS
			long diff_rss = (long)new_r->rss - (long)old_r->rss;
			if(diff_rss < 0) diff_rss = -diff_rss;


			//verificare cele 3 cond (|rss_new - rss_old| >= 1024 sau state diferit sau ppid diferit)
			if(diff_rss >= 1024 || old_r->state != new_r->state || old_r->ppid != new_r->ppid){
				fprintf(out, "PID %d (%s)\n", new_r->pid, new_r->comm);

				if(old_r->state != new_r->state){
					fprintf(out, " state: %c -> %c\n", old_r->state, new_r->state);
				}
				if(old_r->ppid != new_r->ppid){
					fprintf(out, " ppid: %d -> %d\n", old_r->ppid, new_r->ppid);
				}
				if(diff_rss >= 1024){
					fprintf(out, " rss: %lu KB -> %lu KB\n", (unsigned long)old_r->rss, (unsigned long)new_r->rss);
				}
			}
		}
	}
}






//preia argumentele --old, --new, --out
//deschide cele 3 fisiere
//citeste cele 2 headere si verifica daca sunt compatibile
//scrie antetul raportului
//apeleaza diff_files sau diff_proc pe baza tipului de db
int main(int argc, char* argv[]){
	char* old_path = NULL;
	char* new_path = NULL;
	char* out_path = NULL;
	
	for(int i = 1; i < argc; i++){
		if(strcmp(argv[i], "--old") == 0 && i + 1 < argc){
			old_path = argv[++i];
		} else if(strcmp(argv[i], "--new") == 0 && i + 1 < argc) {
			new_path = argv[++i];
		} else if(strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
			out_path = argv[++i];
		}
	}
	if(old_path == NULL || new_path == NULL || out_path == NULL){
		fprintf(stderr, "Eroare: argumentele --old, --new si --out sunt obligatorii\n");
		fprintf(stderr, "Utilizare: db_diff --old <db1> --new <db2> -out <raport>\n");
		return 1;
	}

	FILE* f_old = fopen(old_path, "rb");
	if(f_old == NULL){
		fprintf(stderr, "Eroare la deschidere");
		return 1;	
	}

	FILE* f_new = fopen(new_path, "rb");
	if(f_new == NULL){
		fprintf(stderr, "Eroare la deschidere");
		fclose(f_old);
		return 1;
	}

	FILE* f_out = fopen(out_path, "w");
	if(f_out == NULL){
		fprintf(stderr, "Eroare la deschidere");
		fclose(f_old);
		fclose(f_new);
		return 1;
	}

	db_header_t old_header;
	db_header_t new_header;

	int f1 = fread(&old_header, sizeof(db_header_t), 1, f_old);
	int f2 = fread(&new_header, sizeof(db_header_t), 1, f_new);
	
	if(f1 != 1){
		fprintf(stderr, "Citire fara succes");
		fclose(f_old);
		fclose(f_new);
		fclose(f_out);
		return 1;
	}
	if(f2 != 1){
		fprintf(stderr, "Citire fara succes");
		fclose(f_new);
		fclose(f_old);
		fclose(f_out);
		return 1;
	}
	if(verifica_compatibilitate(&old_header, &new_header) == -1){
		fprintf(stderr, "Cele doua fisiere nu sunt compatibile");
		fclose(f_old);
		fclose(f_new);
		fclose(f_out);
		return 1;
	}


	fprintf(f_out, "===RAPORT DB_DIFF===\n");
	fprintf(f_out, "DB type: %.4s\n", old_header.magic);
	fprintf(f_out, "Old snapshot_id: %lu\n", (unsigned long)old_header.snapshot_id);
	fprintf(f_out, "New snapshot_id: %lu\n", (unsigned long)new_header.snapshot_id);
	fprintf(f_out, "Old record count: %lu\n", (unsigned long)old_header.record_count);
	fprintf(f_out, "New record count: %lu\n", (unsigned long)new_header.record_count);
	

	if(memcmp(old_header.magic, MAGIC_INDEX, 4) == 0){
		file_record_t* old_recs = malloc(old_header.record_count * sizeof(file_record_t));
		file_record_t* new_recs = malloc(new_header.record_count * sizeof(file_record_t));

		if(old_recs == NULL || new_recs == NULL){
			fprintf(stderr, "Eroare la alocare memorie\n");
			free(old_recs);
			free(new_recs);
			fclose(f_old);
			fclose(f_new);
			fclose(f_out);
			return 1;
		}
		fread(old_recs, sizeof(file_record_t), old_header.record_count, f_old);
		fread(new_recs, sizeof(file_record_t), new_header.record_count, f_new);

		diff_files(old_recs, old_header.record_count, new_recs, new_header.record_count, f_out);

		free(old_recs);
		free(new_recs);
	} else if(memcmp(old_header.magic, MAGIC_PROC, 4) == 0){
		proc_record_t* old_recs = malloc(old_header.record_count * sizeof(proc_record_t));
		proc_record_t* new_recs = malloc(new_header.record_count * sizeof(proc_record_t));

		if(old_recs == NULL || new_recs == NULL){
			fprintf(stderr, "Eroare la alocare memorie\n");
			free(old_recs);
			free(new_recs);
			fclose(f_old);
			fclose(f_new);
			fclose(f_out);
			return 1;
		}
		fread(old_recs, sizeof(proc_record_t), old_header.record_count, f_old);
		fread(new_recs, sizeof(proc_record_t), new_header.record_count, f_new);
		
		diff_proc(old_recs, old_header.record_count, new_recs, new_header.record_count, f_out);

		free(old_recs);
		free(new_recs);
	} else {
		fprintf(stderr, "Eroare: tip de db necunoscut\n");
		fclose(f_old);
		fclose(f_new);
		fclose(f_out);

		return 1;
	}





	fclose(f_old);
	fclose(f_new);
	fclose(f_out);
	return 0;

}







