/*
	Name: String.hpp
	Copyright: Apache2.0
	Author: CLimber-Rong
	Date: 29/07/23 12:59
	Description: 字符串库
*/
#pragma once

/*
 * 一些网络上的大牛，他们告诉我，我必须在一些函数的结尾加上const限定符，才能支持
 const String var_name;
 * 的用法。
 * 我采纳了他们的建议，感谢他们！
*/

class String {

	public:
		String();   //初始化为空字符串

		String(char *s);	   //初始化，将s复制到this

		String(const String& s);

		bool equals(const String& s) const ;
		//判断this的内容是否与s相等，是则返回true，否则返回false

		//以下的一系列toString函数会将不同的数据类型转为String后保存到this当中，返回this
		String toString(int value);

		String toString(bool value);

		String toString(float value);

		String toString(double value);

		int toInt() const ;

		int toIntX() const ;

		float toFloat() const ;

		double toDouble() const ;

		int length() const ;			   //返回字符串长度

		char at(int index) const ;		   //返回第index个字符

		char* getstr() const ;	
		//返回str，为了内存安全，建议使用c_arr
		//如果你只需要一个只读用的char*字符串，getstr函数足矣

		char* c_arr() const ;
	    //将把String转换成char*类型并返回，返回值是一个存放于堆的char*指针，建议及时free

		bool match_head(String s) const ;
		//从头开始匹配s，匹配成功返回true，否则返回false

		String substring(int start, int end) ;

		String operator=(const String& right_value) ;

		String operator+(const String& right_value) const ;

		String operator+=(const String& right_value) ;

		bool operator==(const String& right_value) const ;

		bool operator!=(const String& right_value) const ;

		char& operator[](int index) ;

		char operator[](int index) const ;

		~String() ;
};