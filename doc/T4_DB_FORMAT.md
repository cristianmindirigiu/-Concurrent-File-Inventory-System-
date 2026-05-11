# Formatul Bazei de Date Binare (INV4)

Documentația descrie formatul binar de persistență al inventarului de fișiere, versiunea 4.

## 1. Layout-ul Fișierului `inventory.db`
Fișierul este compus din trei secțiuni consecutive, scrise binar:

1. **Header (`inv4_header_t`)**: 21 bytes.
2. **File Records (`file_record_t`[])**: Lungime variabilă ($N \times 1112$ bytes).
3. **Worker Stats (`inv4_worker_stat_t`[])**: Lungime fixă în funcție de $N$ workeri ($W \times 80$ bytes).

## 2. Detalii Structuri

### Header
- `magic`: 4 bytes, trebuie să fie exact `"INV4"`.
- `format_version`: 4 bytes (uint32_t), fixat la `1`.
- `complete`: 1 byte (uint8_t). Valoarea `1` indică o scriere atomică reușită.
- `file_record_count`: 8 bytes (uint64_t), numărul total de fișiere.
- `worker_count`: 4 bytes (uint32_t), numărul de procese care au contribuit.

### File Record
Fiecare intrare conține:
- `path`: Calea absolută (max 1024 caractere).
- `size`: Dimensiunea în bytes.
- `mtime`: Timestamp-ul ultimei modificări.
- `sha256`: Hash-ul calculat în format Hex (64 caractere + null terminator).

### Worker Stats (Trailer)
Conține metricele de performanță: PID, Exit Status, Jobs processed, Files emitted, Bytes sum și timpii CPU (User/Sys) colectați prin `getrusage`.

## 3. Limite și Validitate
- **Limita de căi:** 1024 caractere. Căile mai lungi sunt trunchiate.
- **Validitate:** Un fișier DB este considerat valid dacă:
    1. Magic number-ul este corect.
    2. Flag-ul `complete` este `1`.
    3. Dimensiunea fizică a fișierului corespunde cu: `sizeof(header) + count * sizeof(record) + workers * sizeof(stats)`.

## 4. Garanții de Integritate
Scrierea este **atomică**. Managerul scrie datele într-un fișier temporar `data/inventory.db.tmp` și folosește apelul de sistem `rename()` pentru a înlocui baza de date veche doar după ce scrierea a fost finalizată cu succes.
