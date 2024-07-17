default :
	cd ./apps && make -j && cd ..
	cd ./Loader && make -j && cd ..
	cd ./kernel && make -j && cd ..
	cd ./kernel && make img_run