/*
	Name: Main.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 24/02/24 11:59
	Description:
    * 命令行工具
    * 该文件并不属于编译器或虚拟机的范畴，所以使用了平台库
    * 开发者可以自行更改或者建立属于自己的命令行工具
	* 在使用stamon之前，你需要配置环境变量
	* 即：将可执行文件所在目录设为STAMON
*/

#include"Stamon.hpp"

using namespace stamon;

#include"stdio.h"
#include"stdlib.h"
#define STAMON_ENV_VAR "/stamon/bin"
#define STAMON_PRINTF printf
#define STAMON_VM_MEM_LIMIT 16*1024*1024
#define STAMON_IS_GC true


String getNoEndingSeparatorPath(String path);	//获取末尾没有分隔符的路径
int main(int argc, char* argv[]) {
	stamon::c::TokEOF.lineNo = -1;
	stamon::c::TokEOF.type = 64;
	//参数表
	ArrayList<String> args;

	//获取可执行文件路径
	String s(argv[0]);

	if(STAMON_ENV_VAR==NULL) {
		STAMON_PRINTF(
			"stamon: fatal error: missing enviroment variable \"STAMON\"\n");
		return -1;
	}

	String program_path(STAMON_ENV_VAR);
	bool isGC = STAMON_IS_GC;
	int MemLimit = STAMON_VM_MEM_LIMIT;    //默认虚拟机运行内存16m

	for(int i=1; i<argc; i++) {
		args.add(String(argv[i]));
	}

	if(args.empty()) {
		//没有传入任何参数
		STAMON_PRINTF(
		    "stamon: fatal error: too few arguments\n"
		    "please enter \'stamon help\' to get more information.\n"
		);
		return -1;
	}

	String src;
	
	if(args.size()<1) {
		STAMON_PRINTF("stamon: run: too few arguments\n");
		return -1;
	}

	src = args[0];

	for(int i=1,len=args.size(); i<len; i++) {
		if(args[i].equals(String((char*)"--GC=true"))) {
			isGC = true;
		} else if(args[i].equals(String((char*)"--GC=false"))) {
			isGC = false;
		} else if(
			args[i].length()>11
			&&args[i].substring(0, 11).equals(
				String((char*)"--MemLimit=")
			)
		) {
			MemLimit = args[i]
						.substring(11, args[i].length())
						.toInt();
		} else {
			STAMON_PRINTF(
				"stamon: run: bad command\n"
				"please enter \'stamon help\' "
				"to get more information.\n"
			);
			return -1;
		}
	}

	Stamon stamon;

	stamon.Init();

	stamon.run(src, isGC, MemLimit);

	if(stamon.ErrorMsg->empty()==false) {
		STAMON_PRINTF("stamon: run: fatal error:\n");
		for(int i=0,len=stamon.ErrorMsg->size(); i<len; i++) {
			STAMON_PRINTF("%s\n", stamon.ErrorMsg->at(i).getstr());
		}
		return -1;
	}

	return 0;

}

String getNoEndingSeparatorPath(String path) {
	//去除末尾分隔符
	if(path[path.length()-1]=='\\' || path[path.length()-1]=='/') {
		return path.substring(0, path.length()-1);
	}
	return path;
}