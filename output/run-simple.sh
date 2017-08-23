echo raid5 offline disknum 39 capacity 100G >> running-time.txt
../recovery 0 13 3 3 4096 157650 devices.txt ../../trace/Financial1.spc 0 

echo oi-raid offline disknum 39 capacity 100G >> running-time.txt
../recovery 1 13 3 3 4096 157650 devices.txt ../../trace/Financial1.spc 0 

echo raid5 offline disknum 45 15,3 capacity 100G >> running-time.txt
../recovery 0 15 3 3 4096 157150 devices.txt ../../trace/Financial1.spc 0 

echo oi-raid offline disknum 45 15,3 capacity 100G >> running-time.txt
../recovery 1 15 3 3 4096 157150 devices.txt ../../trace/Financial1.spc 0 

if false; then
echo raid5 offline disknum 27 capacity 100G >> running-time.txt
../recovery 0 9 3 3 4096 159550 devices.txt ../../trace/Financial1.spc 0 

echo oi-raid offline disknum 27 capacity 100G >> running-time.txt
../recovery 1 9 3 3 4096 159550 devices.txt ../../trace/Financial1.spc 0 

echo raid5 offline disknum 45 capacity 100G >> running-time.txt
../recovery 0 9 3 5 4096 130950 devices.txt ../../trace/Financial1.spc 0 

echo oi-raid offline disknum 45 capacity 100G >> running-time.txt
../recovery 1 9 3 5 4096 130950 devices.txt ../../trace/Financial1.spc 0 

echo raid5 offline disknum 57 capacity 100G >> running-time.txt
../recovery 0 19 3 3 4096 156440 devices.txt ../../trace/Financial1.spc 0 

echo oi-raid offline disknum 57 capacity 100G >> running-time.txt
../recovery 1 19 3 3 4096 156440 devices.txt ../../trace/Financial1.spc 0 


##online

echo  >> running-time.txt
echo online >> running-time.txt
echo raid5 online disknum 27 capacity 100G >> running-time.txt
../recovery 0 9 3 3 4096 159550 devices.txt ../../trace/Financial1.spc 100 

echo oi-raid online disknum 27 capacity 100G >> running-time.txt
../recovery 1 9 3 3 4096 159550 devices.txt ../../trace/Financial1.spc 100 

echo raid5 online disknum 45 capacity 100G >> running-time.txt
../recovery 0 9 3 5 4096 130950 devices.txt ../../trace/Financial1.spc 100 

echo oi-raid online disknum 45 capacity 100G >> running-time.txt
../recovery 1 9 3 5 4096 130950 devices.txt ../../trace/Financial1.spc 100 

echo raid5 online disknum 57 capacity 100G >> running-time.txt
../recovery 0 19 3 3 4096 156440 devices.txt ../../trace/Financial1.spc 100 

echo oi-raid online disknum 57 capacity 100G >> running-time.txt
../recovery 1 19 3 3 4096 156440 devices.txt ../../trace/Financial1.spc 100 

fi
