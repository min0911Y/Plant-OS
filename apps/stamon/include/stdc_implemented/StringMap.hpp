/*
	Name: StringMap.cpp
	Copyright: Apache 2.0
	Author: 瞿相荣
	Date: 18/01/23 19:14
	Description: 字符串map
*/

#ifndef STRINGMAP_CPP
#define STRINGMAP_CPP

#include"strie.h"
#include"String.hpp"

template <typename T>
class StringMap {
		STRIE* map;
	public:
		StringMap();							//构造方法
		int put(const String& s, T* data); 			//设置键值
		int del(const String& s, T* data);			//删除键值
		T* get(const String& s);					//获取值
		bool containsKey(const String& s);				//是否存在该键
		int clear();							//清空
		int destroy();							//销毁
		bool empty();							//是否为空
		int traverse(TRIE_VISIT visit);			//遍历
		STRIE* getStrie();
		template<typename list_T>
		ArrayList<list_T> getValList();			/*将所有值汇总成一个指定类型的列表*/
};

template <typename T>
StringMap<T>::StringMap() {
	map = InitTrie();
}

template <typename T>
int StringMap<T>::put(const String& s, T* data) {
	return SetTrieKeyVal(map, (unsigned char*)s.getstr(), s.length(), (void*)data);
}

template <typename T>
int StringMap<T>::del(const String& s, T* data) {
	return DelTrieKeyVal(map, (unsigned char*)s.getstr(), s.length());
}

template <typename T>
T* StringMap<T>::get(const String& s) {
	return (T*)GetTrieKeyVal(map, (unsigned char*)s.getstr(), s.length());
}

template <typename T>
bool StringMap<T>::containsKey(const String& s) {
	return TrieExistKeyVal(map, (unsigned char*)s.getstr(), s.length());
}

template <typename T>
int StringMap<T>::clear() {
	return ClearTrie(map);
}

template <typename T>
int StringMap<T>::destroy() {
	return DestroyTrie(map);
}

template <typename T>
bool StringMap<T>::empty() {
	return TrieEmpty(map);
}

template <typename T>
int StringMap<T>::traverse(TRIE_VISIT visit) {
	return TrieTraverse(map, visit);
}

template<typename T>
STRIE* StringMap<T>::getStrie() {
	return map;
}

template<typename T>
template<typename list_T>
ArrayList<list_T> StringMap<T>::getValList() {
	ArrayList<list_T> result;

	/*将所有值汇总成一个指定类型的列表*/

	STACK* stack = InitStack();

	PushStack(stack, map);
	int i=0;
	while(StackEmpty(stack)==0) {
		map = (STRIE*)PopStack(stack);
		if(map!=NULL) {
			int j;
			for(j=0; j<256; j++) {
				PushStack(stack, map->child[j]);
			}
			if(map->isexist==1) {
				result.add(cast_class(list_T, map->data));
			}
		}
		i++;
	}
	DestroyStack(stack);

	return result;
}

#endif
