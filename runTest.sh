#!/bin/bash

FILE="TestResults.txt"

printf "Running the makefile!\n\n"

make -f Makefile

printf "Finished make, continuing with tests:\n"
printf "Results will be saved in $FILE\n\n";

sum=0

# sizes=(1000000 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1000000) 
sizes=(1000000 1000000 1000000 1000000 1000000 1000000 1000000 1000000 1000000 1000000 1000000 1000000 
 1000000 1000000 1000000 1000000 1000000 1000000)

sizeProfiles=(uniform uniform normal normal fixed8 fixed8 fixed16 fixed16 fixed24 fixed24 fixed104 fixed104
fixed200 fixed200 increase increase decrease decrease)
allocProfiles=(oneinthree cluster oneinthree cluster oneinthree cluster oneinthree cluster oneinthree cluster
oneinthree cluster oneinthree cluster oneinthree cluster oneinthree cluster)

#Aktuelles Datum holen und notieren
d=`date +%Y-%m-%d-%H-%M`
echo $d > $FILE
SUMME=0
counter=0
while [ $counter -lt  ${#sizes[@]} ]
do
	printf "\n\033[92mTest $counter: testit %s %s %s %s\n\033[0m" "$$" "${sizes[$counter]}" "${sizeProfiles[$counter]}" "${allocProfiles[$counter]}" >> $FILE
	VALUE=$( ./testit $$ ${sizes[$counter]} | grep '[^\.]' | tee -a $FILE | grep 'Points' | grep -Eo '[+-]?[0-9]+([.][0-9]+)?')
	# VALUE=$(./testit $$ ${sizes[$counter]} | tee -a $FILE | grep 'Points' | grep -Eo '[+-]?[0-9]+([.][0-9]+)?')
	SUMME=$(bc -l <<<"$SUMME+$VALUE")
	printf "#"
	((counter++))
done

printf "\nTotal sum: %s\n" "$SUMME"

# read -p "Press enter to continue"

exit 0;