/*
	Name: BinaryWriter.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 22/02/24 21:52
	Description: 二进制写入器
*/

#ifndef BINARYREADER_HPP
#define BINARYREADER_HPP

#define FILE_ERR { THROW("file opening error") return; }

#include"Exception.hpp"
#include"String.hpp"
#include"stdio.h"

class BinaryReader {
	public:
		FILE* stream;
		int size;
		char* buffer;
		STMException* ex;

		BinaryReader() {}
		BinaryReader(STMException* e, String filename) {
			ex = e;
			stream = fopen(filename.getstr(), "rb");

			if(stream==NULL) FILE_ERR;

			if(fseek(stream, 0, SEEK_END)!=0) FILE_ERR;
			//将文件指针置于底部

			size = ftell(stream);
			//获取文件指针（即文件底部）与文件头部的距离（即文件大小）

			if(size == -1) FILE_ERR;

			buffer = (char*)calloc(1, size+1);    //根据文件大小开辟内存

			if(buffer==NULL)    FILE_ERR;

			if(fseek(stream, 0, SEEK_SET)!=0) FILE_ERR;
			//将文件指针重新置于顶部
		}

		char* read() {
            if(fread(buffer, 1, size, stream)!=size) {
                return NULL;
            }
			return buffer;
        }

		void close() {
			fclose(stream);
		}
};

#undef FILE_ERROR

#endif