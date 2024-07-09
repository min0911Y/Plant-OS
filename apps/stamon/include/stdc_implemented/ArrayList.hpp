/*
	Name: ArrayList
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 28/07/23 17:10
	Description: 动态数组
*/

#pragma once

#include"stmlib.hpp"

template<typename T>
class ArrayList {
		/*
		 * 采用双倍扩容法
		 * 每次新申请内存且list已经存满时，直接双倍扩容list
		 * 这样申请次数就从n次达到log(2,n)次
		 */

		T *list;
		int cache_length;
		int length;

		void realloc_list(int len) {
			if (cache_length <= len) {
				cache_length = len * 2;
				T *rst = new T[cache_length];

				for (int i = 0; i < length && i < len; i++) {
					// 把list的内容尽可能的复制到rst当中
					rst[i] = list[i];
				}
				delete[] list;
				list = rst;
			}
		}

	public:
		/*值得注意的是：正常的ArrayList赋值，实际上是引用传递*/

		ArrayList() {
			list = NULL;
			length = 0;
			cache_length = 0;
		} // 创建一个空列表

		ArrayList(int size) {
			list = new T[size]; // 分配内存
			length = size;
			cache_length = size;
		} // 创建size个元素的列表

		void add(const T &value) {
			realloc_list(length + 1); // 重新分配内存
			list[length] = value;
			length++;
		} // 末尾添加值

		void insert(int index, const T &value) {
			realloc_list(length + 1); // 重新分配内存

			for (int i = length; i > index; i--) {
				list[i] = list[i - 1];
			}
			list[index] = value;
			length++;
		} // 将value插入到[index]

		void erase(int index) {
			for (int i = index; i < length - 1; i++) {
				list[i] = list[i + 1];
			}
			realloc_list(length - 1); // 重新分配内存
			length--;
		} // 删除[index]

		T at(int index) const {
			return list[index];
		} // 获取[index]

		bool empty() const {
			return (length == 0);
		} // 判断是否为空

		void clear() {
			delete[] list;
			list = NULL;
			length = 0;
		} // 清除列表

		int size() const {
			return length;
		} // 获得元素个数

		ArrayList<T> clone() {
			// 复制一个相同的ArrayList
			// 你可以将这个函数用于值传递
			ArrayList<T> rst(length);
			for (int i = 0; i < length; i++) {
				rst[i] = list[i];
			}
			return rst;
		}

		ArrayList<T> operator+(ArrayList<T> src) {
			ArrayList<T> rst = clone();
			for (int i = 0, len = src.size(); i < len; i++) {
				rst.add(src[i]);
			}
			return rst;
		}

		ArrayList<T> operator+=(ArrayList<T> src) {
			return *(this) = *(this) + src;
		}

		T &operator[](int index) {
			return list[index];
		}

		T operator[](int index) const {
			return list[index];
		}
};