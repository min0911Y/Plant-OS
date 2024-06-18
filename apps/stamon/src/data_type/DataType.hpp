/*
	Name: DataType.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 14/08/23 10:08
	Description:
        * 这里对数据类型进行了基本的定义
        * 如果你想要引用所有的数据类型源码，只要写入以下代码即可：
        #include"DataType.hpp"
        using namespace stamon::datatype;
*/

#ifndef DATATYPE_HPP
#define DATATYPE_HPP

namespace stamon {
	namespace datatype {
		enum _DataTypeID {
		    DataTypeID = 0,
		    NullTypeID,
		    IntegerTypeID,
		    FloatTypeID,
		    DoubleTypeID,
		    StringTypeID,
		    SequenceTypeID,
		    ClassTypeID,
		    MethodTypeID,
		    ObjectTypeID
		};

		class DataType;
		class Variable;
		class NullType;
		class IntegerType;
		class FloatType;
		class DoubleType;
		class StringType;
		class SequenceType;
		class ClassType;
		class MethodType;
		class ObjectType;

		class DataType {
				int type;
			public:
				bool gc_flag = false;	//用于在GC时标记这个值是否被遍历到

				DataType(int type_id) {
					type = type_id;
				}
				virtual int getType() const {
					return type;
				}
		};
	}
}

#include"Variable.cpp"
#include"NullType.cpp"
#include"NumberType.cpp"
#include"StringType.cpp"
#include"SequenceType.cpp"
#include"ClassType.cpp"
#include"MethodType.cpp"
#include"ObjectType.cpp"

#endif