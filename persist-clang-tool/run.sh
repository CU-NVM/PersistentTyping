#!/bin/bash

# Check if an argument is provided
if [ -z "$1" ]; then
  echo "Usage: $0 <Exprs>"
  exit 1
fi

# Run different commands based on the argument
case "$1" in
  DS)
    echo "Running Exprs"
    # Place the actual command for 'command2' here
    # For example: df -h
    ~/PersistentTyping/persist-clang-tool/build/bin/persist-tool --pass=root-setup ~/PersistentTyping/DataStructureTests/src/main.cpp -- -I ~/PersistentTyping/DataStructureTests/include/ -I ~/PersistentTyping/persist -std=c++17 -I /usr/local/lib/clang/20/include/ -lpmemobj
    ;;
  Hanoi)
    echo "Running Hanoi"
    # Place the actual command for 'command1' here
    # For example: ls -l
    ~/PersistentTyping/persist-clang-tool/build/bin/persist-tool --pass=root-setup ~/PersistentTyping/Examples/hanoi/user_hanoi.cpp -- -I ~/PersistentTyping/persist -std=c++17 -I /usr/local/lib/clang/20/include/ -lpmemobj
    ;;
  *)
    echo "Invalid argument. Usage: $0 <DS>"
    exit 1
    ;;
esac