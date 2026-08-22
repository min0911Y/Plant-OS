/*
	Name: Variable.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 01/12/23 19:52
	Description: 变量类，Variable类存储着一个DataType*数据（也就是值）
            * 当需要给每个变量（包括数组元素等）赋值时，只需改变DataType*数据即可
*/

#pragma once

#include"DataType.hpp"

namespace stamon::datatype {
	class Variable {
		public:
			DataType* data;
			Variable() {}
			Variable(const Variable& right) {
				data = right.data;
			}
			Variable(DataType* dt) {
				data = dt;
			}
			virtual ~Variable() = default;
	};
} //namespace stamon::datatype