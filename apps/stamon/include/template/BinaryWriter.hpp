/*
	Name: BinaryWriter.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 22/02/24 21:52
	Description: 二进制写入器
*/

#ifndef BINARYWRITER_HPP
#define BINARYWRITER_HPP

#define FILE_ERROR { THROW("file opening error") return; }

#include"Exception.hpp"
#include"String.hpp"

class BinaryWriter {
    public:
        STMException* ex;

        BinaryWriter();
        BinaryWriter(STMException* e, String filename);

        void write(char b);

        void write_i(int n);

        void close();
};

#endif