/*
	Name: BinaryWriter.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 22/02/24 21:52
	Description: 二进制写入器
*/

#pragma once

#define FILE_ERR { THROW("file opening error") return; }

#include"String.hpp"
#include"Exception.hpp"

class BinaryReader {
	public:
		BinaryReader() {}
		BinaryReader(STMException* e, String filename);

		char* read();

		void close();
};

#undef FILE_ERROR