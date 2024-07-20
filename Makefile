default :
	mkdir -p ./build
	cd ./build && cmake .. && make tcc1 && cd ..
	cd ./apps && make -j && cd ..
	cd ./build && make && cd ..

img_run:
	cd ./kernel && make img_run
run:
	cd ./kernel && make run