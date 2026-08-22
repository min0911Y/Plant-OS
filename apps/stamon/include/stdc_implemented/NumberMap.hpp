/*
	Name: NumberMap.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 23/08/23 14:20
	Description: 整数map
*/

#pragma once

#include"stmlib.hpp"
#include"strie.h"
#include"ArrayList.hpp"

template<typename T>
class NumberMap {
		STRIE* map = NULL;
	public:
		NumberMap() {
			map = InitTrie();
		}
		int put(int s, T* data) { 			//设置键值
			return SetTrieKeyVal(map, (unsigned char*)&s, sizeof(int), (void*)data);
		}
		int del(int s) {			//删除键值
			return DelTrieKeyVal(map, (unsigned char*)&s, sizeof(int));
		}
		T* get(int s) {					//获取值
			return (T*)GetTrieKeyVal(map, (unsigned char*)&s, sizeof(int));
		}
		bool containsKey(int s) {				//是否存在该键
			return TrieExistKeyVal(map, (unsigned char*)&s, sizeof(int));
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