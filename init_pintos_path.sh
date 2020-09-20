#!/bin/sh

bashrc=~/.bashrc

baktime=$(date +%Y.%m.%d.%H:%M:%S)
cp $bashrc $bashrc.bak.$baktime
echo "backup file of $bashrc is created. ($baktime)"

# if defined, remove
if grep -q PINTOS_ROOT $bashrc; then
    grep -v "PINTOS_ROOT" $bashrc > tmp && mv tmp $bashrc
fi

# add environment vars
line="export PINTOS_ROOT=$PWD\nexport PATH=\$PINTOS_ROOT/src/utils:\$PATH"
echo "$line" | sudo tee -a $bashrc
