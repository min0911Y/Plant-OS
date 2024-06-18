/*
	Name: StringMap.cpp
	Copyright: Apache 2.0
	Author: 瞿相荣
	Date: 18/01/23 19:14
	Description: 字符串map
*/

#ifndef STRINGMAP_CPP
#define STRINGMAP_CPP

#include"String.hpp"

template <typename T>
class StringMap {
	public:
		StringMap();							//构造方法
		int put(String s, T* data); 			//设置键值
		int del(String s, T* data);			//删除键值
		T* get(String s);					//获取值
		bool containsKey(String s);				//是否存在该键
		int clear();							//清空
		int destroy();							//销毁
		bool empty();							//是否为空
		int traverse(TRIE_VISIT visit);			//遍历
		STRIE* getStrie();
		template<typename list_T>
		ArrayList<list_T> getValList();			/*将所有值汇总成一个指定类型的列表*/
};

#endif
