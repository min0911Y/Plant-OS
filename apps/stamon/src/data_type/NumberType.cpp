/*
	Name: NumberType.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 14/08/23 10:09
	Description: 数字类型的定义，包括整数和浮点数
*/

#pragma once

#include"DataType.hpp"
#include"stmlib.hpp"

namespace stamon::datatype {
	class IntegerType : public DataType {
			int val;
		public:
			IntegerType(int value) : DataType(IntegerTypeID) {
				val = value;
			}
			virtual int getVal() const {
				return val;
			}
			virtual ~IntegerType() = default;
	};

	class FloatType : public DataType {
			float val;
		public:
			FloatType(float value) : DataType(FloatTypeID) {
				val = value;
			}
			virtual float getVal() {
				return val;
			}
			virtual FloatType toThisType(DataType src) {
				//这个函数用于把低等数据类型转为高等数据类型
				//由于比float低等的只有int，所以直接把int转float就可以了
				IntegerType src_int = cast_class(IntegerType, src);
				int src_val = src_int.getVal();
				FloatType result((float)src_val);
				return result;
			}
			virtual ~FloatType() = default;
	};

	class DoubleType : public DataType {
			double val;
		public:
			DoubleType(double value) : DataType(DoubleTypeID) {
				val = value;
			}
			virtual double getVal() {
				return val;
			}
			virtual DoubleType toThisType(DataType src) {
				if(src.getType()==IntegerTypeID) {
					//整数转双精度浮点数
					IntegerType src_int = cast_class(IntegerType, src);
					int src_val = src_int.getVal();
					DoubleType result((double)src_val);
					return result;
				} else {
					//单精度浮点数转双精度浮点数
					FloatType src_float = cast_class(FloatType, src);
					int src_val = src_float.getVal();
					DoubleType result((double)src_val);
					return result;
				}
			}
			virtual ~DoubleType() = default;
	};
} //namespace stamon::datatype