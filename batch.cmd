echo "INFO: This is a TEST shell script in batch mode for simple_shell ..."

echo
echo "TEST 0: Executing: rm -rf TEST_DIR"
rm -rf TEST_DIR

echo
echo "TEST 1: Executing: mkdir TEST_DIR"
mkdir TEST_DIR

echo
echo "TEST 2: Executing builtin command: pwd"
pwd

echo
echo "TEST 3: Executing builtin command: cd TEST_DIR"
cd TEST_DIR

echo
echo "TEST 4: "Executing builtin command: pwd"
pwd

echo
echo "TEST 5. Executing: touch empty_file"
touch empty_file

echo
echo "TEST 6. Executing: ls -la output to  file_list"
ls -la > file_list

echo
echo "TEST 7: Executing: ls -la | grep empty"
ls -la  | grep empty

echo
echo "TEST 8: Executing: mkdir -p x/y/z"
mkdir -p x/y/z

echo
echo "TEST 9: Executing: touch x/another_empty_file"
touch x/another_empty_file

echo
echo "TEST 10: Executing: ls -l x..."
ls -l x

echo
echo "TEST 11: Executing: ls -l *file*"
ls -ls *file*

echo "Command after then executed" > then
echo "Command after else executed" > else

echo
echo "TEST 12: Executing command after then to test then/else syntax:"
touch new_empty_file
then cat then
else cat else

echo
echo "TEST 13: Executing command after else to test then/else syntax:"
ls -l file_does_not_exist
then cat then
else cat else

echo
echo "TEST 14: Executing built in command: which ls"
which ls

echo
echo "TEST 15: Executing: cat read_from file_list pipe2 grep empty"
cat < file_list | grep empty

echo
echo "TEST 16: Executing: cd .."
cd ..
pwd

echo
echo "TEST 17: Executing: cat read_from batch.cmd pipe2 grep TEST pipe2 grep 11"
cat < batch.cmd | grep -i test | grep 11
