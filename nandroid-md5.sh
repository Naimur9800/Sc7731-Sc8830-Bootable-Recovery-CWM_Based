#!/sbin/sh
# carliv @ xda-developers fix the generate

folder=$1;
rm -f /tmp/nandroid.md5;
cd $folder;
find . -type f -exec md5sum {} + > /tmp/nandroid.md5;
cp /tmp/nandroid.md5 ./;
# need this because wildcard seems to cause md5sum to return 1
if [ -f nandroid.md5 ];
then
  return 0;
else
  return 1;
fi;
