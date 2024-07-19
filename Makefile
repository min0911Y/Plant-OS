default :
	cd ./apps && make -j && cd ..
	mkdir -p ./build && cd ./build && cmake .. && make && cd ..

img_run:
	cd ./kernel && make img_run
run:
	cd ./kernel && make run