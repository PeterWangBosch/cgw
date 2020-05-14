#!/bin/bash

#su
#if [ $? != 0 ]
#then
#    echo "[ERROR] Make sure you're root user."
#    exit 1
#fi

mkdir /vendor/bin/ -p
mkdir /etc/orchestrator/ -p
mkdir /var/orchestrator/ -p

cp orchestrator /vendor/bin/
cp config.ini /etc/orchestrator/

echo "Orchestrator has been installed in /vendor/bin/"
