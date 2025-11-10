#!/bin/env bash

FILES=`find . -type f -name compile_commands.json`

for file in $FILES; do
    if [[ "$file" == *"build"* ]]; then
        ln -s $file compile_commands.json
        echo "Created symlink compile-commands.json => $file"
    fi;
done;
