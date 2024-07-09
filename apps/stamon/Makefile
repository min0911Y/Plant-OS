# 可以更改$(COMPILER)以及$(REMOVE)来自定义编译环境
COMPILER = g++
STRIP = strip
UPX = upx
REMOVE = del

# 以下指令用于兼容先前调试时的编译指令

clean:
	- $(REMOVE) test.exe

build: clean
	$(COMPILER) test/test.cpp \
	-o test.exe \
	-O0 \
	-std=c++11 \
	-I include/stdc_implemented \
	-I src/ast \
	-I src/data_type \
	-I src/vm \
	-I src/ir \
	-I src/compiler \
	-I src/sfn \
	-I src \
	-lm

zip: test.exe
	$(STRIP) -s test.exe
	$(UPX) -9 test.exe

run: test.exe
	./test.exe

debug: bin/stamon.exe
	bin/stamon.exe build code.st code.stvc



# 以下指令用于编译发行版


release: release_win
#	编译平台默认为Windows平台

release_win:
	$(COMPILER) src/Main.cpp \
	-o bin/stamon.exe \
	-O2 \
	-std=c++11 \
	-I include/stdc_implemented \
	-I src/ast \
	-I src/data_type \
	-I src/vm \
	-I src/ir \
	-I src/compiler \
	-I src/sfn \
	-I src \
	-lm

	$(STRIP) -s bin/stamon.exe
	$(UPX) -9 bin/stamon.exe

release_linux:
#	编译Linux版本
	$(COMPILER) src/Main.cpp \
	-o bin/stamon \
	-O2 \
	-std=c++11 \
	-I include/stdc_implemented \
	-I src/ast \
	-I src/data_type \
	-I src/vm \
	-I src/ir \
	-I src/compiler \
	-I src/sfn \
	-I src \
	-lm

	$(STRIP) -s bin/stamon
	$(UPX) -9 bin/stamon



LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
CPPFLAGS =  -m32 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-exceptions -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -Qn -O3 -march=pentium -fno-rtti -w
release_plos:
	#	编译Plant OS版本
	$(CPP) src/Main.cpp \
	-c -o ../out/stamon.obj \
	-O0 \
	-std=c++11 \
	-I include/stdc_implemented \
	-I src/ast \
	-I src/data_type \
	-I src/vm \
	-I src/ir \
	-I src/compiler \
	-I src/sfn \
	-I src \
	-lm
	$(LINK) ../out/stamon.obj $(BASIC_LIB_CPP) -o ../out/stamon.bin