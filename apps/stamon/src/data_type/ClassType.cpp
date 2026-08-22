/*
	Name: ClassType.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 14/08/23 11:54
	Description:
    * 在STVM中，类本身也是一个值，它对应的数据类型就是ClassType
    * 这里就写上了ClassType的定义
*/

#pragma once

#include"DataType.hpp"
#include"Ast.hpp"

namespace stamon::datatype {
	class ClassType : public DataType {
			ast::AstAnonClass* val;
		public:
			ClassType(ast::AstAnonClass* value) : DataType(ClassTypeID) {
				val = value;
			}
			virtual ast::AstAnonClass* getVal() const {
				return val;
			}
			virtual ~ClassType() = default;
	};
} //namespace stamon::datatype