# Protocolul de Comunicare IPC (Memory-Mapped)

Acest document descrie mecanismul de comunicare multiproces utilizat în Tema 4 pentru coordonarea între procesul Manager și procesele Worker.

## 1. Structura Memoriei Partajate (`ipc_shared_t`)
Memoria partajată este mapată folosind `mmap` cu flag-ul `MAP_SHARED` peste un fișier fizic situat în `data/ipc.mmap`. Layout-ul este segmentat în trei zone principale:

### A. Coada de Job-uri (`job_queue_t`)
Este o coadă circulară (Ring Buffer) globală.
- **Capacitate:** 256 de intrări (`JOB_QUEUE_CAPACITY`).
- **Rol:** Managerul introduce directorul rădăcină; Workerii consumă job-uri și introduc subdirectoarele descoperite.

### B. Canalele de Rezultate (`result_channels`)
Fiecare worker (până la `MAX_WORKERS=16`) are un canal dedicat de tip `result_channel_t`.
- **Capacitate:** 64 de înregistrări per canal (`RESULT_CHANNEL_CAPACITY`).
- **Design:** Această separare elimină contenția pe semafoare, permițând scrieri simultane din partea workerilor.

### C. Statisticile Workerilor (`worker_stats_t`)
Tablou de structuri unde workerii scriu în timp real PID-ul, numărul de fișiere procesate și timpii de execuție (Wall/User/Sys).

## 2. Sincronizare și Backpressure
Sincronizarea este realizată strict prin semafoare POSIX anonime:

- **Excludere Mutuală (Mutex):** Fiecare structură de tip coadă/canal are un semafor binar `mutex` care protejează pointerii de `head`, `tail` și contorul `count`.
- **Backpressure:** Implementat prin semaforul `slots_libere` (inițializat cu capacitatea buffer-ului). Dacă un producător (Manager sau Worker) încearcă să adauge date într-un buffer plin, acesta este blocat de sistemul de operare până când un consumator eliberează un slot.
- **Notificare Consumator:** Realizată prin semaforul `slots_ocupate`, care trezește procesul consumator imediat ce datele sunt disponibile.

## 3. Condiția de Terminare
Sistemul folosește o regulă de terminare deterministă:
Scanarea se consideră finalizată atunci când `job_queue.count == 0` ȘI `job_queue.jobs_in_progress == 0`. În acel moment, Managerul setează `terminate_flag = true` și „trezește” toți workerii blocați prin postări succesive pe semaforul de job-uri.
