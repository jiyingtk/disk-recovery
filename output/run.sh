echo raid5 offline disknum 27 capacity 100G >> running-time.txt
../recovery 0 9 3 3 4096 102400 devices.txt ../../trace/Financial1.spc 0 

echo oi-raid offline disknum 27 capacity 100G >> running-time.txt
../recovery 1 9 3 3 4096 102400 devices.txt ../../trace/Financial1.spc 0 

echo raid5 offline disknum 45 capacity 100G >> running-time.txt
../recovery 0 9 3 5 4096 102400 devices.txt ../../trace/Financial1.spc 0 

echo oi-raid offline disknum 45 capacity 100G >> running-time.txt
../recovery 1 9 3 5 4096 102400 devices.txt ../../trace/Financial1.spc 0 

echo raid5 offline disknum 57 capacity 100G >> running-time.txt
../recovery 0 19 3 3 4096 102400 devices.txt ../../trace/Financial1.spc 0 

echo oi-raid offline disknum 57 capacity 100G >> running-time.txt
../recovery 1 19 3 3 4096 102400 devices.txt ../../trace/Financial1.spc 0 

##online
echo  >> running-time.txt
echo online >> running-time.txt
echo raid5 online disknum 27 capacity 100G >> running-time.txt
../recovery 0 9 3 3 4096 102400 devices.txt ../../trace/Financial1.spc 30 

echo oi-raid online disknum 27 capacity 100G >> running-time.txt
../recovery 1 9 3 3 4096 102400 devices.txt ../../trace/Financial1.spc 30 

echo raid5 online disknum 45 capacity 100G >> running-time.txt
../recovery 0 9 3 5 4096 102400 devices.txt ../../trace/Financial1.spc 30 

echo oi-raid online disknum 45 capacity 100G >> running-time.txt
../recovery 1 9 3 5 4096 102400 devices.txt ../../trace/Financial1.spc 30 

echo raid5 online disknum 57 capacity 100G >> running-time.txt
../recovery 0 19 3 3 4096 102400 devices.txt ../../trace/Financial1.spc 30 

echo oi-raid online disknum 57 capacity 100G >> running-time.txt
../recovery 1 19 3 3 4096 102400 devices.txt ../../trace/Financial1.spc 30 


