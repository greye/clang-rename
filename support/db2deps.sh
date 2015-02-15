#!/bin/sh
sed '/"command":/,1 {
    s/"command":\s*"//
    s/",$//
    s/.*/echo $(eval & -MM)/e
    s/\\\s\s*/\ /g
    s/.*:\s*//
    s/[^[:space:]][^[:space:]]*/echo $(realpath &)/ge
    s/echo//g
    s/[^[:space:]][^[:space:]]*//
    s/[^[:space:]][^[:space:]]*/"&"/g
    s/"\s*"/", "/g
    s/.*/  "deps": [&],/
}' "$1"


