/*
	Name: SequenceType.cpp
	Copyright: Apache 2.0
	Author: Climber-Rong
	Date: 14/08/23 10:19
	Description: 数列数据类型的定义
    * 也许你会对这种数据类型的名字感到困惑
    * 数列其实是数组和列表的结合
    * 数列可以在创建的时候就初始化长度，这点与类似Python的列表不同
    * 数列也可以动态的改变长度，这点与类似C++的数组不同
    * 数列这种数据结构很大程度上使得这个虚拟机可以兼容更多的语言，而且它的实现也并非难事
*/

#ifndef SEQUENCETYPE_CPP
#define SEQUENCETYPE_CPP

#include"DataType.hpp"
#include"ArrayList.hpp"

namespace stamon {
	namespace datatype {
		class SequenceType : public DataType {
			public:
				ArrayList<Variable*> sequence;
				SequenceType(int length) : DataType(SequenceTypeID), sequence(ArrayList<Variable*>(length)) {
				}
				SequenceType(ArrayList<Variable*> value) : DataType(SequenceTypeID) {
					sequence = value;
				}
				virtual ArrayList<Variable*> getVal() const {
					return sequence;
				}
		};
	}
}

#endif