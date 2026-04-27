# FORMAT_DB.md : Documentatia formatului bazelor de date binare

Acest document descrie formatul binar folosit de `fileops_indexer`, `proc_snapshot` si `db_diff` pentru fisierele `index.db` si 
`proc.db`.

## 1. Structura generala a fisierului

Ambele baze de date au aceeasi organizare:

```
---------------------- offset 0
|       HEADER       |  cu dimensiune fixa
----------------------
|       RECORD 0     |  cu dimensiune fixa (in functie de tip)
----------------------
|       RECORD 1     |
----------------------
|        ...         |
----------------------
|      RECORD N-1    |
----------------------

```

Record-urile au dimensiune fixa pentru a permite accesul direct prin offset (adica recordul N incepe la `sizeof(header) + N * sizeof(record)`).


## 2. Structura header-ului

Definita in `include/db_format.h`, structura `db_header_t`:

| Camp             | Tip            | Dimensiune   | Descriere                                                |
|------------------|----------------|--------------|----------------------------------------------------------|
| `magic`          | `char[4]`      | 4 bytes      | Identificator de tip fisier: `"IDXI"` sau `"PRCI"`       |
| `format_version` | `uint32_t`     | 4 bytes      | Versiunea formatului binar (curent `1`)                  |
| `snapshot_id`    | `uint64_t`     | 8 bytes      | Identificator unic al snapshotului (timestamp Unix)      |
| `snapshot_state` | `uint8_t`      | 1 byte       | `0 = OPEN`, `1 = SEALED`                                 |
| `active_writers` | `uint32_t`     | 4 bytes      | Numar de instante care contribuie la snapshot            |
| `record_count`   | `uint64_t`     | 8 bytes      | Numar total de inregistrari in baza de date              |


**Valori pentru `magic`:**
- `"IDXI"` - baza de date de fisiere (`index.db`)
- `"PRCI"` - baza de date de procese (`proc.db`)

**Valori pentru `snapshot_state`:**
- `SNAPSHOT_OPEN = 0` - snapshot in curs de completare, accepta contributii noi
- `SNAPSHOT_SEALED = 1` - snapshotul este finalizat si nu mai accepta contributii

## 3. Structura record-urilor

### 3.1. Record pentru `index.db` - `file_record_t`


| Camp                | Tip                   | Descriere                                         |
|---------------------|-----------------------|---------------------------------------------------|
| `path`              | `char[MAX_PATH_LEN]`  | Cale absoluta fata de `/` (cheia logica)          |
| `type`              | `uint8_t`             | Tipul intrarii (de mai jos)                       |
| `size`              | `uint64_t`            | Dimensiunea in bytes (doar fisiere regulate)      |
| `mtime`             | `time_t`              | Timpul ultimei modificari                         |
| `st_dev`            | `uint64_t`            | Device ID din `struct stat`                       |
| `st_ino`            | `uint64_t`            | Inode number din `struct stat`                    |
| `hash`              | `uint32_t`            | Hash djb2 al continutului (doar fisiere regulate) |
| `symlink_target`    | `char[MAX_PATH_LEN]`  | Tinta symlink-ului (doar pentru symlink-uri)      |


**Valori pentru campul `type`:**
- `0` - fisier regulat
- `1` - director
- `2` - symlink
- `3` - FIFO (named pipe)
- `4` - alte tipuri (device, socket etc)

**Algoritm Hash:** djb2
```
hash = 5381
for byte in content: 
    hash = ((hash << 5) + hash) + byte   # hash * 33 + byte
```

**Symlink-uri:** sunt inregistrate ca symlink-uri (adica nu sunt dereferentiate si nu se face parcurgere recursiva prin ele)
Campul `symlink_target` contine tinta citita cu `readlink(2)`.

### 3.2 Record pentru `proc.db` - `proc_record_t`

| Camp                | Tip                    | Descriere                                          |
|---------------------|------------------------|----------------------------------------------------|
| `pid`               | `pid_t`                | PID-ul procesului (cheia logica)                   |
| `ppid`              | `pid_t`                | PID-ul parintelui                                  |
| `state`             | `char`                 | Starea procesului (R, S, D, Z, T etc)              |
| `comm`              | `char[MAX_COMM_LEN]`   | Numele executabilului                              |
| `cmdline`           | `char[MAX_CMDLINE_LEN]`| Linia de comanda, trunchiata la MAX_CMDLINE_LEN-1  |
| `rss`               | `uint64_t`             | Resident Set Size in KB (din `/proc/<pid>/status`) |
| `cpu_time`          | `uint64_t`             | Timp CPU total in ticks (utime + stime din stat)   |


**Sursa campurior:**
- `pid`, `ppid`, `state`, `comm`, `cpu_time` -> `/proc/<pid>/stat`
- `rss` -> campul `VmRSS:` din `/proc/<pid>/status`, exprimat in KB
- `cmdline` -> `/proc/<pid>/cmdline`


**cpu_time** este suma `utime + stime` din `/proc/<pid>/stat`, exprimata in ticks de sistem(un tick = `1/sysconf(_SC_CLK_TCK)`
secunde, de obicei 1/100 secunde)


## 4. Limite

Sunt definite in `include/db_format.h`:

| Constanta         | Valoare     | Semnificatie                               |
|-------------------|-------------|--------------------------------------------|
| `MAX_PATH_LEN`    | 1024        | Lungime maxima pentru cai si tinte symlink |
| `MAX_COMM_LEN`    | 256         | Lungime maxima pentru numele executabilului|
| `MAX_CMDLINE_LEN` | 1024        | Lungime maxima pentru linia de comanda     |

Toate stringurile sunt null-terminated. Continutul care depaseste aceste limite este trunchiat (ultimul byte ramane `\0`)


## 5. Strategia de actualizare

Toate operatiunile de modificare respecta urmatoarele reguli: 


**La deschiderea bazei de date:**

1. Se ia lock exclusiv pe header
2. Daca fisierul e nou, se initializeaza header-ul:
    - `magic` -> `"IDXI"` / `"PRCI"`
    - `format_version` -> 1
    - `snapshot_id` -> `time(NULL)`
    - `snapshot_state` -> `OPEN`
    - `active_writers` -> 1
    - `record_count` -> 0
3. Daca fisierul exista cu `snapshot_state == OPEN` atunci se reutilizeaza `snapshot_id` si se incrementeaza `active_writers`
4. Daca fisierul exista cu `snapshot_state == SEALED` atunci instanta se opreste cu mesaj explicit
5. Lock-ul pe header e eliberat



**La scrierea unui record:**

1. Lock exclusiv pe header
2. Se cauta recordul dupa cheia logica (path pentru `index.db`, PID pentru `proc.db`)
3. Daca exista, se retine offset-ul. Daca nu exista, se aloca un slot nou la finalul fisierului si se incrementeaza `record_count` in header.
4. Se elibereaza lock-ul de pe header
5. Lock exclusiv pe regiunea record-ului
6. Se scrie record-ul la offset-ul determinat
7. Se elibereaza lock-ul pe record


**La inchiderea contributiei:**

1. Lock exclusiv pe header
2. Se decrementeaza `active_writers`
3. Daca `active_writers == 0`, atunci `snapshot_state` devine SEALED
4. Se elibereaza lock-ul de pe header

Orice record nou este precedat de o cautare dupa cheia logica. De asemenea, nu exista dubluri(cheia logica este unica per snapshot)


## 6. Conditii de validitate

O baza de date e valida daca:

1. Dimensiunea fisierului >= `sizeof(db_header_t)`
2. `magic` corespunde tipului asteptat (`"IDXI"` sau `"PRCI"`)
3. `format_version` este suportat de utilitar (curent : `1`)
4. Dimensiunea fisierului este exact `sizeof(db_header_t) + record_count * sizeof(record_t)`
5. `snapshot_state` este `OPEN` (0) sau `SEALED` (1)

Utilitarele care detecteaza o baza invalida se opresc cu mesaj de eroare, fara a modifica fisierul.



## 7. Reguli de comparare folosite de `db_diff`

`db_diff` valideaza ca ambele fisiere de intrare au acelasi `magic` si acelasi `format_version`. Daca nu, da eroare.

### 7.1. Comparare pentru `index.db`

Cheia de comparare : **calea absoluta** (`path`)

- **Aparut** -- path prezent in `new`, absent in `old`
- **Disparut** -- path prezent in `old`, absent in `new`
- **Modificat** -- path prezent in ambele, dar cel putin in unul din campurile urmatoare difera:
    - `type`
    - `size`
    - `mtime`
    - `hash`
    - `symlink_target`


Raport: `reports/T3_filediff.txt`

### 7.2. Comparare pentru `proc.db`

Cheia de comparare : **PID** (`pid`)

- **Aparut** -- PID prezent in `new`, absent in `old`
- **Disparut** -- PID  prezent in `old`, absent in `new`
- **Modificat** --  PID prezent in ambele, dar cel putin una din conditiile urmatoare e indeplinita:
    - Diferenta absoluta `|rss_new - rss_old|` >= 1MB(1024KB)
    - `state` diferit
    - `ppid` diferit

Am ales pragul de 1MB pentru RSS pentru a raporta doar schimbarile semnificative.

Raport : `reports/T3_procdiff.txt`


## 8. Conventie de naming pentru fisierele DB

- Fisier activ implicit : `data/index.db` si `data/proc.db`
- Argumentul `--db <path>` permite o alta cale
- Snapshot-ul este identificat prin `(tipul_DB, snapshot_id)` nu prin numele fisierului.
- Pentru pastrarea separata a mai multor snapshot-uri se recomanda redenumirea fisierelor active in `data/index_<snapshot_id>.db`  si `data/proc_<snapshot_id>.db` dupa finalizare





