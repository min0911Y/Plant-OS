/*
	Name: Main.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 24/02/24 11:59
	Description:
    * 命令行工具
    * 该文件并不属于编译器或虚拟机的范畴，所以使用了平台库
    * 开发者可以自行更改或者建立属于自己的命令行工具
	* P.S. 该文件能且大概率仅能在Windows平台上正常运作
	* 在使用stamon之前，你需要配置环境变量
	* 即：将可执行文件所在目录设为STAMON
*/

#include"Stamon.hpp"

using namespace stamon;

#include"stdio.h"
#include"stdlib.h"
#include <syscall.h>

void getHelpInformation();  //输出帮助信息
int a8 = 0;
String getNoEndingSeparatorPath(String path);	//获取末尾没有分隔符的路径
#define getenv(a) "/stamon/bin"
int main(int argc, char* argv[]) {
	stamon::c::TokEOF.lineNo = -1;
	stamon::c::TokEOF.type = 64;
	//参数表
	ArrayList<String> args;

	//获取可执行文件路径
	String s(argv[0]);

	if(getenv("STAMON")==NULL) {
		printf(
		    "stamon: fatal error: missing enviroment variable \"STAMON\"\n"
		    "please enter \'stamon help\' to get more information.\n"
		);
		return -1;
	}

	String program_path(getenv("STAMON"));

	for(int i=1; i<argc; i++) {
		args.add(String(argv[i]));
	}

	if(args.empty()) {
		//没有传入任何参数
		printf(
		    "stamon: fatal error: too few arguments\n"
		    "please enter \'stamon help\' to get more information.\n"
		);
		return -1;
	}


	if(
	    args[0].equals(String((char*)"build"))
	    ||args[0].equals(String((char*)"-b"))
	) {
		String src;
		printf("FUCK STRING\n");
		String dst((char*)"a.stc");
		printf("FUCK OFF\n");
		bool isSupportImport = true;    //默认支持import

		//解析编译的文件名

		if(args.size()>=2) {
			src = args[1];

			//目标文件名是可选的，默认a.stvc
			if(args.size()==3) {
				dst = args[2];
			} else {
				for(int i=3; i<args.size(); i++) {

					if(args[i].equals(String((char*)"--import=false"))) {
						printf("3\n");
						isSupportImport = false;

					} else if(args[i].equals(String((char*)"--import=true"))) {
						printf("2\n");
						isSupportImport = true;

					} else if(
						printf("1\n");
					    args[i].length()>3
					    &&args[i].substring(0, 2).equals((char*)"-I")) {

						//添加引用路径
						ImportPaths.add(
						    getNoEndingSeparatorPath(
						        args[i].substring(2, args[i].length())
						    )
						);

					} else {

						//错误参数
						printf(
						    "stamon: compile: bad command\n"
						    "please enter \'stamon help\' "
						    "to get more information.\n"
						);

						return -1;
					}

				}
			}
		}

		//printf(program_path.c_arr());
		printf("here\n");
		if(isSupportImport) {
			a8 = 1;
			printf("1\n");
			ImportPaths.insert(
			    0,
			    getNoEndingSeparatorPath(program_path)
			    + String((char*)"/include/")
			);
			//加入标准库路径
			a8 = 0;
		}

		Stamon stamon;
		printf("init\n");
		stamon.Init();
		printf("START THE FUCKING COMPILER\n");
		stamon.compile(src, dst, isSupportImport);

		if(stamon.ErrorMsg->empty()==false) {
			printf("stamon: compile: fatal error:\n");
			for(int i=0,len=stamon.ErrorMsg->size(); i<len; i++) {
				printf("%s\n", stamon.ErrorMsg->at(i).getstr());
			}
			return -1;
		}

		return 0;

	} else if(
	    args[0].equals(String((char*)"run"))
	    ||args[0].equals(String((char*)"-r"))
	) {

		String src;
		bool isGC = true;

		int MemLimit = 16*1024*1024;    //默认虚拟机运行内存16m

		if(args.size()<2) {
			printf("stamon: run: too few arguments\n"
			       "please enter \'stamon help\' "
			       "to get more information.\n");
		} else {
			src = args[1];

			for(int i=2,len=args.size(); i<len; i++) {
				if(args[i].equals(String((char*)"--GC=true"))) {
					isGC = true;
				} else if(args[i].equals(String((char*)"--GC=false"))) {
					isGC = false;
				} else if(
				    args[i].length()>12
				    &&args[i].substring(0, 11).equals(
				        String((char*)"--MemLimit=")
				    )
				) {
					MemLimit = args[i]
					           .substring(11, args[i].length())
					           .toInt();
				} else {
					printf(
					    "stamon: run: bad command\n"
					    "please enter \'stamon help\' "
					    "to get more information.\n"
					);
					return -1;
				}
			}
		}

		Stamon stamon;

		stamon.Init();

		printf("CNMB\n");
		stamon.run(src, isGC, MemLimit);

		if(stamon.ErrorMsg->empty()==false) {
			printf("stamon: run: fatal error:\n");
			for(int i=0,len=stamon.ErrorMsg->size(); i<len; i++) {
				printf("%s\n", stamon.ErrorMsg->at(i).getstr());
			}
			return -1;
		}

		return 0;

	} else if(
	    args[0].equals(String((char*)"help"))
	    ||args[0].equals(String((char*)"-h"))
	) {
		getHelpInformation();
		return 0;
	} else if(
	    args[0].equals(String((char*)"version"))
	    ||args[0].equals(String((char*)"-v"))
	) {
		printf(
		    "stamon %d.%d.%d\n"
		    "Be Released by CLimber-Rong(github.com/CLimber-Rong/)\n"
		    "Open Source in \'github.com/CLimber-Rong/stamon/\'\n"
		    "This program has absolutely no warranty.\n",
			STAMON_VER_X, STAMON_VER_Y, STAMON_VER_Z
		);
		return 0;
	} else {
		printf(
		    "stamon: compile: bad command\n"
		    "please enter \'stamon help\' "
		    "to get more information.\n"
		);
		return -1;
	}

	return 0;

}



void getHelpInformation() {
	printf(
	    "Usage: stamon file [options] ...\n"
	    "Options\n"
	    "\tversion | -v\t\t\tDisplay this version.\n"
	    "\thelp | -h\t\t\tDisplay this information.\n"
	    "\tbuild | -b\t\t\tBuild this source to program.\n"
	    "\t\t<filename>\t\tSource filename\n"
	    "\t\t--import=<boolean>\t\tSupport Import Flag\n"
	    "\t\t-I<path>\t\tAdd Include Path\n"
	    "\trun | -r\t\t\tRun STVC.\n"
	    "\t\t--GC=<boolean>\t\tGC Flag\n"
	    "\t\t--MemLimit=<Integer>\tSet VM Memory Limit\n"
	);
}

String getNoEndingSeparatorPath(String path) {
	//去除末尾分隔符
	if(path[path.length()-1]=='\\' || path[path.length()-1]=='/') {
		return path.substring(0, path.length()-1);
	}
	return path;
}