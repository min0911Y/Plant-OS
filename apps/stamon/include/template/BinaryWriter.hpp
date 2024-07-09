/*
	Name: BinaryWriter.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 22/02/24 21:52
	Description: 二进制写入器
*/

#pragma once

#define FILE_ERROR { THROW("file opening error") return; }

#include"Exception.hpp"
#include"String.hpp"

class BinaryWriter {
    public:

        BinaryWriter() {}
        BinaryWriter(STMException* e, String filename);

        void write(char b);

        void write_i(int n);

        void close();
};

#undef FILE_ERROR