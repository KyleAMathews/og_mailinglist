#!/bin/bash
echo "====ORIGINAL EMAIL====="
cat $1

echo "====NEW EMAIL===="
./newsystemtest.php < $1


echo '===DIFF====='

diff <(./newsystemtest.php < $1) $1
