/*
	Name:
	Copyright:
	Author:
	Date: 14/08/23 10:10
	Description: 空值数据类型定义
*/

#pragma once

#include"DataType.hpp"

namespace stamon::datatype {
	class NullType : public DataType {
		public:
			NullType() : DataType(NullTypeID) {}
			virtual ~NullType() = default;
	};
} //namespace stamon::datatype