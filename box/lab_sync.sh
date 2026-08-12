#!/bin/bash
# Sync a lab branch to its box slot and rebuild. Run from the MAIN repo.
# Usage: box/lab_sync.sh <branch> <box> <lab>
set -e
BRANCH=$1; BOX=$2; LAB=$3
REMOTE=/content/labs/$LAB
git archive "$BRANCH" | ssh "$BOX" "mkdir -p $REMOTE && tar x -C $REMOTE"
ssh "$BOX" "cd $REMOTE && mkdir -p build && clang++ -std=c++17 -O3 -ffast-math -Iinclude src/codec.cpp src/main.cpp -o build/brushie -pthread && echo BUILT $REMOTE/build/brushie"
ln -sfn /content/brushie/datasets "$REMOTE/datasets"
