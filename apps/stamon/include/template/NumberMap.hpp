/*
	Name: NumberMap.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 23/08/23 14:20
	Description: 以整数为键的map
*/

#ifndef NUMBERMAP_HPP
#define NUMBERMAP_HPP

#include"stmlib.hpp"
#include"strie.h"
#include"ArrayList.hpp"

template<typename T>
class NumberMap {
	public:
		NumberMap();
		int put(int s, T* data)	//设置键值
		int del(int s)	//删除键值
		T* get(int s);	//获取值
		bool containsKey(int s);
		int clear();
		int destroy();
		bool empty();

		template<typename list_T>
		ArrayList<list_T> getValList(); /*将所有值汇总成一个指定类型的列表*/

};

#endif