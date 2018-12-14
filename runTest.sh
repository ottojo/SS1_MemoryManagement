#!/bin/bash

FILE="TestResults.txt"

printf "Running the makefile!\n\n"

make -f Makefile

printf "Finished make, continuing with tests:\n"
printf "Results will be saved in $FILE\n\n";

sum=0

# Run each of the 18 profiles with 10 different sizes (180 tests)

sizes=(5 100 500 1000 5000 10000 50000 100000 500000 1000000 )
sizeProfiles=(uniform uniform normal normal fixed8 fixed8 fixed16 fixed16 fixed24 fixed24 fixed104 fixed104 fixed200
    fixed200 increase increase decrease decrease)
allocProfiles=(oneinthree cluster oneinthree cluster oneinthree cluster oneinthree cluster oneinthree cluster oneinthree
    cluster oneinthree cluster oneinthree cluster oneinthree cluster)

#Aktuelles Datum holen und notieren
d=`date +%Y-%m-%d-%H-%M`
echo ${d} > ${FILE}
SUMME=0
c2=0
while [[ ${c2} -lt  ${#sizes[@]} ]]
do
    counter=0
    while [[ ${counter} -lt  ${#sizeProfiles[@]} ]]
    do
        printf "\n\033[92mTest $counter: testit %s %s %s %s\n\033[0m" "$$" "${sizes[$counter]}" "${sizeProfiles[$counter]}" "${allocProfiles[$counter]}" >> ${FILE}
        VALUE=$( ./testit $$ ${sizes[c2]} | grep '[^\.]' | tee -a ${FILE} | grep 'Points' | grep -Eo '[+-]?[0-9]+([.][0-9]+)?')
        # VALUE=$(./testit $$ ${sizes[$counter]} | tee -a $FILE | grep 'Points' | grep -Eo '[+-]?[0-9]+([.][0-9]+)?')
        SUMME=$(bc -l <<<"$SUMME+$VALUE")
        printf "Size $c2 Profile $counter done.\n"
        ((counter++))
    done
    ((c2++))
done

printf "\nTotal sum: %s\n" "$SUMME"

exit 0;