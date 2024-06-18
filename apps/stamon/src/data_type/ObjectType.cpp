/*
	Name: ObjectType.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 17/08/23 11:49
	Description:
    * 在STVM中，新建的类对象也是一个值，它对应的数据类型就是ObjectType
    * 这里就写上了ObjectType的定义
*/

#ifndef OBJECT_CPP
#define OBJECT_CPP

#include"DataType.hpp"
#include"NumberMap.hpp"

namespace stamon {
	namespace datatype {
		class ObjectType : public DataType {
				NumberMap<Variable> vals;  //由于STVM内部以数字来代替标识符，所以采用NumberMap
			public:
				ObjectType(NumberMap<Variable> value)
					: DataType(ObjectTypeID), vals(NumberMap<Variable>()) {
					vals = value;
				}
				virtual NumberMap<Variable> getVal() const {
					return vals;
				}
		};

	}
}

#endif