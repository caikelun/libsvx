#!/bin/bash

if [ $# -eq 0 ]
then
    echo "Usage: $(basename $0) your-program [your-program-options]"
    exit 1
fi

valgrind --tool=memcheck \
         --leak-check=full \
         --show-reachable=yes \
         --track-origins=yes \
         --track-fds=yes \
         --num-callers=50 \
         $*
