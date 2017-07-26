rm devices.txt
for ((i=1; i<=21; i++))
do
	echo ../../img/$i.img >> devices.txt
	dd if=/dev/zero of=../../img/$i.img bs=1M count=10
done
