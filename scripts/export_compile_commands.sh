#!/bin/env bash

set -e
if [ -e compile_commands.json ]; then
    echo "hERE"
    rm compile_commands.json
fi

FILES=`find . -type f -name compile_commands.json`
jq -s add $FILES > ./compile_commands.json
echo "$FILES got merged into ./compile_commands.json"
