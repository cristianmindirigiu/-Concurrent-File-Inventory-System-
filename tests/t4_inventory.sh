#!/bin/bash

echo "[TEST T4] Incepere test automat pentru File Inventory Builder..."

# Definim caile pentru test
TEST_DIR="tmp/test_t4_scenario"
DB_FILE="data/inventory.db"
IPC_FILE="data/ipc.mmap"
DUMP_OUT="tmp/dump_out.txt"

# 1. Creare scenariu de test (un arbore de directoare fals, cu 4 fisiere in total)
echo "[TEST T4] 1. Pregatire scenariu in $TEST_DIR"
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR/dir1/dir2"
mkdir -p "$TEST_DIR/dir3"

echo "Fisier de test 1" > "$TEST_DIR/file1.txt"
echo "Fisier de test 2 cu alt continut" > "$TEST_DIR/dir1/file2.txt"
touch "$TEST_DIR/dir1/dir2/empty.log"
echo "Ultimul fisier de test" > "$TEST_DIR/dir3/file3.txt"

# 2. Rulare manager cu 2 workeri
echo "[TEST T4] 2. Rulare fileops_manager cu 2 workeri..."
./bin/fileops_manager --root "$TEST_DIR" --workers 2 --ipc "$IPC_FILE" --db "$DB_FILE"
MANAGER_STATUS=$?

if [ $MANAGER_STATUS -ne 0 ]; then
    echo "[TEST T4] FAIL: Managerul a returnat o eroare (cod $MANAGER_STATUS)."
    exit 1
fi

# 3. Verificarea existentei bazei de date
echo "[TEST T4] 3. Verificare existenta baza de date..."
if [ ! -f "$DB_FILE" ]; then
    echo "[TEST T4] FAIL: Fisierul binar $DB_FILE nu a fost generat!"
    exit 1
fi
echo "[TEST T4] Baza de date $DB_FILE a fost gasita."

# 4. Rulare --verify
echo "[TEST T4] 4. Verificare integritate DB (--verify)..."
./bin/fileops_manager --verify --db "$DB_FILE"
VERIFY_STATUS=$?

if [ $VERIFY_STATUS -ne 0 ]; then
    echo "[TEST T4] FAIL: Verificarea bazei de date a esuat (cod $VERIFY_STATUS)."
    exit 1
fi

# 5. Rulare --dump si verificarea campurilor stabile
echo "[TEST T4] 5. Rulare --dump si verificare campuri stabile..."
./bin/fileops_manager --dump --db "$DB_FILE" > "$DUMP_OUT"

# Verificam rand pe rand fiecare camp folosind grep
if ! grep -q "magic=INV4" "$DUMP_OUT"; then
    echo "[TEST T4] FAIL: Câmpul 'magic=INV4' lipseste sau este incorect in output-ul dump."
    exit 1
fi

if ! grep -q "complete=1" "$DUMP_OUT"; then
    echo "[TEST T4] FAIL: Câmpul 'complete=1' lipseste sau este incorect (baza de date ar putea fi corupta)."
    exit 1
fi

if ! grep -q "file_record_count=4" "$DUMP_OUT"; then
    echo "[TEST T4] FAIL: Câmpul 'file_record_count' nu este 4, desi am creat 4 fisiere de test."
    exit 1
fi

if ! grep -q "worker_count=2" "$DUMP_OUT"; then
    echo "[TEST T4] FAIL: Câmpul 'worker_count' nu este 2, desi am pornit 2 workeri."
    exit 1
fi

echo "[TEST T4] Câmpurile (magic, complete, file_record_count, worker_count) au fost validate cu succes in urma comenzii --dump."

echo "[TEST T4] SUCCESS! Toate verificarile au trecut."
exit 0
