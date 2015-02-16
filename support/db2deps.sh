#!/bin/sh
sed '
  /"command":/,1 {
    s/"command":\s*"\(.*\)",$/&\
"deps": \1/
  }' $1 |
sed '
  /"deps":/,1 {
    s/"deps": //
    s/.*/echo $(eval & -MM)/e
    s/\\\s\s*/\ /g
    s/.*:\s*//
    s/[^[:space:]][^[:space:]]*/echo $(realpath &)/ge
    s/echo//g
    s/[^[:space:]][^[:space:]]*//
    s/[^[:space:]][^[:space:]]*/"&"/g
    s/"\s*"/", "/g
    s/.*/  "deps": [&],/
  }'
