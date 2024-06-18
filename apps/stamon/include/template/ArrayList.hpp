/*
	Name: ArrayList
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 28/07/23 17:10
	Description: 动态数组
*/

#ifndef ARRAYLIST
#define ARRAYLIST

#include"stdlib.h"

using namespace std;

template <typename T>
class ArrayList {
	public:
		/*值得注意的是：正常的ArrayList赋值，实际上是引用传递*/
	
		ArrayList();//创建一个空列表

		ArrayList(int size);//创建size个元素的列表

		void add(const T& value); //末尾添加值

		void insert(int index, const T& value);	//将value插入到[index]

		void erase(int index);	//删除[index]

		T at(int index) const ;		//获取[index]

		bool empty() const ;	//判断是否为空

		void clear() ;	//清除列表

		int size() const ;	//获得元素个数

		ArrayList<T> clone();	//克隆一个ArrayList

		ArrayList<T> operator+(ArrayList<T> src);	//将两个ArrayList拼接

		ArrayList<T> operator+=(ArrayList<T> src);

		T& operator[](int index);	//取下标

		T operator[](int index) const ;	//取下标


};

#endif
