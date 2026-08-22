/*
	Name: FileMap.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 05/02/24 20:50
	Description: 以文件为键的map
*/

#pragma once

#include"Exception.hpp"
#include"StringMap.hpp"
#include"LineReader.hpp"
#include"String.hpp"

class FileMap {
	public:
		STMException* ex;

		FileMap() {}

		FileMap(STMException* e) {
			ex = e;
		}

		LineReader mark(const String& filename);
		/*
			* 将该文件标记，并且返回打开该文件后的LineReader
			* 如果该文件不存在或出错，抛出异常
		*/

		bool exist(const String& filename);
};