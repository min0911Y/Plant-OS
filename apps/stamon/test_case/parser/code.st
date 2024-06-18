func QuickSort(arr, low, high) {
    if low < high {   //不加这个条件会死循环 
        def i = low, j = high, key = arr[low];

        while i<j {
            while i<j && arr[j]>=key:
                j -= 1;
            if i<j {
                arr[i] = arr[j];
                i += 1;     //先赋值再自增 
            } 
                                                    
            while i<j && arr[i]<key:
                i += i;

            if i<j {
                arr[j] = arr[i];
                j -= 1;
            }
				
        }
        arr[i] = key;
        QuickSort(arr, low, i-1);//给左边排序 
        QuickSort(arr, i+1, high);//给右边排序 
    }
}