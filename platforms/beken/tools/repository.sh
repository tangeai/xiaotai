#!/usr/bin/env bash

# Print the XiaoTai repository root for a project at any nesting depth.
xiaotai_find_repository_root() {
    local candidate="$1"
    while [[ ! -f "$candidate/boards/schema/board.schema.json" ]]; do
        local parent
        parent=$(dirname "$candidate")
        if [[ "$parent" == "$candidate" ]]; then
            echo "Cannot locate XiaoTai repository root from $1" >&2
            return 1
        fi
        candidate="$parent"
    done
    printf '%s\n' "$candidate"
}
