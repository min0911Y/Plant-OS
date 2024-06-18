/*
	Name: BinaryWriter.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 22/02/24 21:52
	Description: 二进制写入器
*/

#ifndef BINARYREADER_HPP
#define BINARYREADER_HPP

#define FILE_ERROR { THROW("file opening error") return; }

#include"Exception.hpp"
#include"String.hpp"

class BinaryReader {
	public:
		STMException* ex;

		BinaryReader() {}
		BinaryReader(STMException* e, String filename);

		char* read() ;

		void close();
};

#undef FILE_ERROR

#endif