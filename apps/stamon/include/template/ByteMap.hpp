/*
	Name: ByteMap.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 10/02/24 13:38
	Description: 字节串map
*/

#ifndef BYTEMAP_HPP
#define BYTEMAP_HPP

#include"strie.h"
#include"ArrayList.hpp"

template<typename T>
class ByteMap {
	public:
		ByteMap() ;

		int put(char* s, int size, T* data) ;	//设置键值
		int del(char* s, int size);	//删除键值
		T* get(char* s, int size);	//获取值
		bool containsKey(char* s, int size);	//是否存在该键
		int clear();	//清空
		int destroy();	//销毁
		bool empty();	//是否为空
		
		template<typename list_T>
		ArrayList<list_T> getValList();	//将所有值汇总成一个指定类型的列表
};

#endif