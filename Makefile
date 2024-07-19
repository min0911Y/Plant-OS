default :
	cd ./apps && make -j && cd ..
	cd ./loader && make -j && cd ..
	cd ./kernel && make -j && cd ..
	cd ./kernel && make img_run