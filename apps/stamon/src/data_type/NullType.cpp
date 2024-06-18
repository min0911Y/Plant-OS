/*
	Name:
	Copyright:
	Author:
	Date: 14/08/23 10:10
	Description: 空值数据类型定义
*/

#ifndef NULLTYPE_CPP
#define NULLTYPE_CPP

#include"DataType.hpp"

namespace stamon {
	namespace datatype {
		class NullType : public DataType {
			public:
				NullType() : DataType(NullTypeID) {}
		};
	}
}

#endif