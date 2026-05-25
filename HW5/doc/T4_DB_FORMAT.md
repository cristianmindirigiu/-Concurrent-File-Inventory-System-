# Formatul Bazei de Date Binare (Inventory DB)

Baza de date este scrisa doar de manager. Workerii nu scriu in DB.

## Atomicitate

Scrierea se face in fisier temporar (inventory.db.tmp) urmat de rename() peste fisierul final. Daca managerul pica la jumatate, fisierul vechi ramane intact.

## Layout

### A. Header (inv4_header_t)

Offset 0, contine:
- magic: "INV4" (4 bytes). Daca nu e INV4, DB-ul e invalid.
- format_version: 1
- complete: 0 (DB_START), 1 (DB_PART_COMPLETE) sau 2 (DB_FULL_COMPLETE)
- file_record_count: numarul de fisiere inregistrate
- worker_count: cati workeri au contribuit

### B. File Records (file_record_t[])

Imediat dupa header, un array de file_record_count inregistrari. Fiecare contine:
- path: calea absoluta a fisierului
- size: dimensiunea in bytes
- mtime: timestamp ultima modificare
- mode: permisiuni POSIX
- uid, gid
- sha256: hash-ul fisierului in format hex (64 caractere)

### C. Worker Stats (inv4_worker_stat_t[])

Dupa records, worker_count structuri cu statistici per worker:
- worker_id, pid, exit_status
- jobs_processed, files_emitted, bytes_sum
- time_ms, user_cpu_t, sys_cpu_t

## Validare (--verify)

DB-ul e considerat valid structural daca:
- Header-ul se citeste complet
- magic == "INV4"
- format_version == 1
- complete == 1 (DB_PART_COMPLETE) sau 2 (DB_FULL_COMPLETE)
- Dimensiunea fisierului coincide cu sizeof(header) + records * sizeof(record) + workers * sizeof(stat)

Un DB cu complete=1 nu e corupt. Inseamna ca inventarierea a fost intrerupta dar structura e intacta.
