#!/bin/bash

FILE="TestResults-$(git rev-parse --short HEAD).txt"

printf "Running the makefile!\n\n"

make -f Makefile

printf "Finished make, continuing with tests:\n"
printf "Results will be saved in $FILE\n\n";

# Run each of the 18 profiles with 10 different sizes (180 tests)

sizes=(5 100 500 1000 5000 10000 50000 100000 500000 1000000 )
sizeProfiles=(uniform uniform normal1 normal1 fixed8 fixed8 fixed16 fixed16 fixed24 fixed24 fixed104 fixed104 fixed200
    fixed200 increase increase decrease decrease)
allocProfiles=(oneinthree cluster oneinthree cluster oneinthree cluster oneinthree cluster oneinthree cluster oneinthree
    cluster oneinthree cluster oneinthree cluster oneinthree cluster)

SUMME=0
profileIndex=0

echo "" > ${FILE}

while [[ ${profileIndex} -lt  ${#sizeProfiles[@]} ]]
do
    echo "\"${sizeProfiles[profileIndex]} ${allocProfiles[profileIndex]}\"" >> ${FILE}
    sizeIndex=0
    while [[ ${sizeIndex} -lt  ${#sizes[@]} ]]
    do
        echo "Testing Profile ${sizeProfiles[$profileIndex]} ${allocProfiles[$profileIndex]}, Size ${sizes[sizeIndex]}"
        VALUE=$(./testit 1 ${sizes[sizeIndex]} ${sizeProfiles[$profileIndex]} ${allocProfiles[$profileIndex]} | grep '[^\.]' | grep 'Points' | grep -o -E -e '[+\-\.0-9]*')
        SUMME=`echo ${SUMME} + ${VALUE} | bc`
        echo "${sizes[sizeIndex]},${VALUE}" >> ${FILE}
        echo "Profile $profileIndex, Size $sizeIndex done."
        ((sizeIndex++))
    done
    printf "\n\n" >> ${FILE}
    ((profileIndex++))
done

printf "\nTotal sum: %s\n" "$SUMME"

gnuplot -c plot.gp ${FILE} > "TestResults-$(git rev-parse --short HEAD).png"

exit 0;