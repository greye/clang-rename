#!/bin/sh
# pass all header and implementation files as arguments to generate
# fake compile commands database in json format

CXX="g++"
CPPFLAGS="$(llvm-config --cppflags)"

dbname="compile_commands.json"
rm -f $dbname
touch $dbname
echo "[" >> $dbname
for file in $@
do
    path=$(realpath $file)
    echo "{" >> $dbname
    echo "  \"directory\": \"$(dirname $path)\"," >> $dbname
    echo "  \"command\": \"$CXX -x c++ -std=c++11 $CPPFLAGS -c $path\"," >> $dbname
    echo "  \"file\": \"$path\"" >> $dbname
    echo "}," >> $dbname
done
echo "]" >> $dbname
