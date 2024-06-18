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
#include"stdio.h"

class BinaryWriter {
    public:
        FILE* fstream;
        STMException* ex;

        BinaryWriter() {}
        BinaryWriter(STMException* e, String filename) {
            ex = e;
            fstream = fopen(filename.getstr(), "wb");
            if(fstream==NULL) FILE_ERROR
        }

        void write(char b) {
            if(!fwrite(&b, 1, 1, fstream)) FILE_ERROR
        }

        void write_i(int n) {
            if(!fwrite(&n, 4, 1, fstream)) FILE_ERROR
        }

        void close() {
            fclose(fstream);
        }
};

#undef FILE_ERROR

#endif