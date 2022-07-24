#!/bin/bash
fifo_name="test-pipe-ctrl"
while true
do
    if read line; then
        echo $line
    fi
done <"$fifo_name"
