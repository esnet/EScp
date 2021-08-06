cd /tmp
mkdir -p test-src test-dst
echo "Args: " $1 $2
$1 -x -X 512M -f test-src/512M.$2 -b 1M -t 4 --nodirect --engine $2
$1 -s -f test-dst/512M.$2 -c localhost --nodirect --engine $2 --verbose --logfile test-src/rx.log &
JOB=$!
$1 -c localhost -f test-src/512M.$2 -t 4 --nodirect --engine $2 --verbose --logfile test-src/tx.log
echo -n "Verifying that file matches using $2... "
if [ \"`sha1sum test-src/512M.$2 | cut -f 1 -d \ `\" = \"`sha1sum test-dst/512M.$2 | cut -f 1 -d \ `\" ]; then
  echo "Passed"
else
  echo "Failed!";
  kill $JOB
  exit 1
fi



