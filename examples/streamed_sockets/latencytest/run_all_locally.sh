#!/bin/bash

trap "kill 0" EXIT

NDEBUG=1 make -j .current/generator .current/indexer .current/forward .current/processor .current/terminator || exit 1

./.current/terminator --silent &
./.current/processor &
./.current/forward $* &
./.current/indexer $* &
./.current/generator &

wait
