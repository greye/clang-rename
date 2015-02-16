#!/bin/sh
# pass all header and implementation files as arguments to generate
# fake compile commands database in json format

CXX="g++"
#for dir in $($CXX -E -xc++ - -v < /dev/null 2>&1 | grep ^\ /usr | grep include)
#do
#    SYSINC="$SYSINC -isystem $dir"
#done
SYSINC="-isystem $(llvm-config-3.5 --includedir)"

CXXFLAGS="$(llvm-config --cxxflags)"

echo "["
for file in $@
do
    path=$(realpath $file)
    echo "{
  \"directory\": \"$(dirname $path)\",
  \"command\": \"$CXX -c $SYSINC $CXXFLAGS $path\",
  \"file\": \"$path\"
},"
done
echo "]"
