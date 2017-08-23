method_name=('raid-5' 'oi-raid' 'rs69' 's2-raid' 'parity-declustering')
request_num=(0 100)
request_type=('offline' 'online')
for i in $(seq 0 `expr ${#request_num[@]} - 1`)
do

for ((method=2;method<=4;method++))
do
    echo ${method_name[$method]} ${request_type[$i]} disknum 21 capacity 100G >> running-time.txt
    ../recovery $method 7 3 3 4096 102400 devices.txt ../../trace/Financial1.spc ${request_num[$i]}

    echo ${method_name[$method]} ${request_type[$i]} disknum 27 capacity 100G >> running-time.txt
    ../recovery $method 9 3 3 4096 102400 devices.txt ../../trace/Financial1.spc ${request_num[$i]}

    echo ${method_name[$method]} ${request_type[$i]} disknum 45 capacity 100G >> running-time.txt
    ../recovery $method 15 3 3 4096 102400 devices.txt ../../trace/Financial1.spc ${request_num[$i]} 

    echo ${method_name[$method]} ${request_type[$i]} disknum 57 capacity 100G >> running-time.txt
    ../recovery $method 19 3 3 4096 102400 devices.txt ../../trace/Financial1.spc ${request_num[$i]} 
done

done

