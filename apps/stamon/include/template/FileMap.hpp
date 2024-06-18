/*
	Name: FileMap.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 05/02/24 20:50
	Description: 以文件为键的map
*/

#ifndef FILEMAP_HPP
#define FILEMAP_HPP

#include"String.hpp"
#include"Exception.hpp"
#include"LineReader.hpp"

class FileMap {
	public:
		STMException* ex;

		FileMap();

		FileMap(STMException* e) {
			ex = e;
		}

		LineReader mark(String filename);

		/*
		 * 将该文件标记，并且返回打开该文件后的LineReader
		 * 如果该文件不存在或出错，抛出异常
		 */

		bool exist(String filename);	//某个文件是否已经被标记过
};

#endif