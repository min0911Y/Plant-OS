/*
	Name: NumberMap.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 23/08/23 14:20
	Description: 整数map
*/

#pragma once

#include"stmlib.hpp"
#include"ArrayList.hpp"

template<typename T>
class NumberMap {
	public:
		NumberMap();
		int put(int s, int size, T* data); //设置键值
		int del(int s, int size); //删除键值
		T* get(int s, int size); //获取值
		bool containsKey(int s, int size); //是否存在该键
		int clear(); //清空
		int destroy(); //销毁
		bool empty(); //是否为空

		template<typename list_T>
		ArrayList<list_T> getValList();//将所有值汇总成一个指定类型的列表
};