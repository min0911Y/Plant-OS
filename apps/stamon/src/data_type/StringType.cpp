/*
	Name: StringType.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 14/08/23 10:10
	Description: 字符串类型的定义
*/

#pragma once

#include"DataType.hpp"
#include"String.hpp"

namespace stamon::datatype {
	class StringType : public DataType {
			String val;
		public:
			StringType(const String& value) : DataType(StringTypeID) {
				val = value;
			}
			virtual String getVal() const {
				return val;
			}
			virtual ~StringType() = default;
	};
} //namespace stamon::datatype