#!/bin/bash

echo "TEST T4 : Incepere test automat pentru File Inventory Builder"

TEST_DIR="tmp/test_t4_scenario"
DB_FILE="data/inventory.db"
IPC_FILE="data/ipc.mmap"
DUMP_OUT="tmp/dump_out.txt"

#creare scenariu de test
echo "TEST T4 : Pregatire scenariu in $TEST_DIR"
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR/dir1/dir2"
mkdir -p "$TEST_DIR/dir3"

echo "Fisier de test 1" > "$TEST_DIR/file1.txt"
echo "Fisier de test 2 cu alt continut" > "$TEST_DIR/dir1/file2.txt"
touch "$TEST_DIR/dir1/dir2/empty.log"
echo "Ultimul fisier de test" > "$TEST_DIR/dir3/file3.txt"

#rulare manager
echo "TEST T4 : Rulare fileops_manager cu 2 workeri"
./bin/fileops_manager --root "$TEST_DIR" --workers 2 --ipc "$IPC_FILE" --db "$DB_FILE"
MANAGER_STATUS=$?

if [ $MANAGER_STATUS -ne 0 ]; then
    echo "TEST T4 FAIL: Managerul a returnat eroare (cod $MANAGER_STATUS)."
    exit 1
fi

#verificare existenta DB
echo "TEST T4 : Verificare existenta baza de date"
if [ ! -f "$DB_FILE" ]; then
    echo "TEST T4 FAIL : Fisierul $DB_FILE nu a fost generat"
    exit 1
fi
echo "TEST T4 : Baza de date gasita."

#--verify
echo "TEST T4 : Verificare integritate DB"
./bin/fileops_manager --verify --db "$DB_FILE"
VERIFY_STATUS=$?

if [ $VERIFY_STATUS -ne 0 ]; then
    echo "TEST T4 FAIL : Verificarea a esuat (cod $VERIFY_STATUS)."
    exit 1
fi

#--dump si verificare campuri
echo "TEST T4 : Verificare campuri stabile din --dump"
./bin/fileops_manager --dump --db "$DB_FILE" > "$DUMP_OUT"

if ! grep -q "magic=INV4" "$DUMP_OUT"; then
    echo "TEST T4 FAIL : magic=INV4 lipseste"
    exit 1
fi

if ! grep -q "complete=2" "$DUMP_OUT"; then
    echo "TEST T4 FAIL : complete=2 lipseste (inventariere completa)"
    exit 1
fi

if ! grep -q "file_record_count=4" "$DUMP_OUT"; then
    echo "TEST T4 FAIL : file_record_count nu este 4"
    exit 1
fi

if ! grep -q "worker_count=2" "$DUMP_OUT"; then
    echo "TEST T4 FAIL : worker_count nu este 2"
    exit 1
fi

echo "TEST T4 : Campurile au fost validate cu succes"
echo "TEST T4 : SUCCESS"
exit 0
