/*
	Name: LineReader.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 03/02/24 16:07
	Description: 行阅读器
*/

#ifndef LINEREADER_HPP
#define LINEREADER_HPP

#include"String.hpp"
#include"Exception.hpp"

class LineReader {
	public:
		String getLine();
		bool isMore();
};

#endif