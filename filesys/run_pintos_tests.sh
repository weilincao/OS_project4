#!/bin/bash

# Change this to have the test(s) you want to run dir-mkdir
TEST_FILES='
grow-dir-lg
args-none
grow-file-size
grow-seq-sm
grow-seq-lg
'

for file in $TEST_FILES; do

    # add line spacing between tests
    echo ""

    # find which folder the test is in
    DIR=`find . -name $file`

    if [ -z "$DIR" ]; then
        echo "ERROR: test not found"
    else
        # remove the old output file, run the test, output results
        rm $DIR.output 2> /dev/null
        make $DIR.result 2> /dev/null
        cat $DIR.output 2> /dev/null
        cat $DIR.result 2> /dev/null
    fi

done
