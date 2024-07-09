/*
	Name: ByteMap.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 10/02/24 13:38
	Description: 字节串map
*/

#pragma once

#include"strie.h"
#include"ArrayList.hpp"

template<typename T>
class ByteMap {
		STRIE* map;
	public:
		ByteMap() {
			map = InitTrie();
		}
		int put(char* s, int size, T* data) { 			//设置键值
			return SetTrieKeyVal(map, (unsigned char*)&s, size, (void*)data);
		}
		int del(char* s, int size) {			//删除键值
			return DelTrieKeyVal(map, (unsigned char*)&s, size);
		}
		T* get(char* s, int size) {					//获取值
			return (T*)GetTrieKeyVal(map, (unsigned char*)&s, size);
		}
		bool containsKey(char* s, int size) {				//是否存在该键
			return TrieExistKeyVal(map, (unsigned char*)&s, size);
		}
		int clear() {							//清空
			return ClearTrie(map);
		}
		int destroy() {							//销毁
			return DestroyTrie(map);
		}
		bool empty() {							//是否为空
			return TrieEmpty(map);
		}

		STRIE* getStrie() {
			return map;
		}

		template<typename list_T>
		ArrayList<list_T> getValList() {
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
};