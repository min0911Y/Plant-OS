/*
	Name: LineReader.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 03/02/24 16:07
	Description: 行阅读器
*/

#pragma once

#include"Exception.hpp"
#include"String.hpp"
#include"ArrayList.hpp"

#define FILE_ERR { THROW("file opening error") return; }
//这个宏用于简写，并且该宏只能在本文件中使用

ArrayList<String> ImportPaths;

class LineReader {
	public:
		STMException* ex;
		
		LineReader() {}

		LineReader(String filename, STMException* e);

		String getLine();

		bool isMore();

		void close();
};

#undef FILE_ERR