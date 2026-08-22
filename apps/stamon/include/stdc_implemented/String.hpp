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

#include"stdlib.h"
#include"string.h"
#include"stdio.h"

class String {

		/*
		 * 利用了缓存技术
		 * 对于短字符串（长度小于32字符），采用栈数组存储，减少内存申请次数
		 */

		bool isUseCache = false;
		char* str;
		char cache[32] = {0};

		void* StrCalloc(int size, int count) {
			if(size*count<=32) {
				isUseCache = true;
				int i = 0;
				while((cache[i]!=0) && (i<32)) {
					cache[i] = 0;
					i++;
				}
				return cache;
			} else {
				isUseCache = false;
				return calloc(size, count);
			}
		}

		void StrFree() {
			if(!isUseCache) {
				free(str);
			}
		}

	public:
		String() {
			str = (char*)StrCalloc(1,1);
		}			   //初始化为空字符串

		String(char *s) {
			str = (char*)StrCalloc(strlen(s)+1, 1);
			strcpy(str, s);
		}	   //初始化，将s复制到this

		String(const String& s) {
			str = s.c_arr();
		}

		bool equals(const String& s) const {
			if(length()!=s.length()) {
				//长度不一
				return false;
			}

			for(int i=0,len=s.length(); i<len; i++) {
				if(str[i]!=s[i]) {
					return false;
				}
			}

			return true;
		} //判断this的内容是否与s相等，是则返回true，否则返回false

		//以下的一系列toString函数会将不同的数据类型转为String后保存到this当中，返回this
		String toString(int value) {
			StrFree();
			str = (char*)StrCalloc(256, 1);
			sprintf(str, "%d", value);
			str = (char*)realloc(str, strlen(str)+1);
			return String(str);
		}

		String toString(bool value) {
			str = (char*)StrCalloc(10, 1);

			if(value==true) {
				str = (char*)realloc(str, 5);
				strcpy(str, "true");
			} else {
				str = (char*)realloc(str, 6);
				strcpy(str, "false");
			}

			return String(str);
		}

		String toString(float value) {
			StrFree();
			str = (char*)StrCalloc(256, 1);
			sprintf(str, "%f", value);
			str = (char*)realloc(str, strlen(str)+1);
			return String(str);
		}

		String toString(double value) {
			StrFree();
			str = (char*)StrCalloc(256, 1);
			sprintf(str, "%lf", value);
			str = (char*)realloc(str, strlen(str)+1);
			return String(str);
		}

		int toInt() const {
			int rst;
			sscanf(str, "%d", &rst);
			return rst;
		}

		int toIntX() const {
			int rst;
			sscanf(str, "%x", &rst);
			return rst;
		}

		float toFloat() const {
			float rst;
			sscanf(str, "%f", &rst);
			return rst;
		}

		double toDouble() const {
			double rst;
			sscanf(str, "%lf", &rst);
			return rst;
		}

		int length() const {
			return strlen(str);
		}			   //返回字符串长度

		char at(int index) const {
			return str[index];
		}		   //返回第index个字符

		char* getstr() const {
			return str;
		}	//返回str，为了内存安全，建议使用c_arr，如果你只需要一个只读用的char*字符串，getstr函数足矣

		char* c_arr() const {
			char* rst = (char*)calloc(strlen(str)+1, 1);
			strcpy(rst, str);
			return rst;
		}	   //将把String转换成char*类型并返回，返回值是一个存放于堆的char*指针，建议及时free

		bool match_head(String s) const {
			if(length()<s.length()) {
				//长度过小
				return false;
			}

			for(int i=0,len=s.length(); i<len; i++) {
				if(str[i]!=s[i]) {
					return false;
				}
			}

			return true;
		} //从头开始匹配s，匹配成功返回true，否则返回false

		String substring(int start, int end) {
			//获取从start到end（不包含end）的子字符串

			if(start==end) {
				return String((char*)"");
			}

			char* s = (char*)calloc(end-start+1, 1);

			for(int i=0,len=end-start; i<len; i++) {
				s[i] = str[start+i];
			}

			String rst(s);
			free(s);
			return rst;
		}

		String operator=(const String& right_value) {
			StrFree();

			str = (char*)StrCalloc(right_value.length()+1, 1);

			strcpy(str, right_value.getstr());

			return String(str);
		}

		String operator+(const String& right_value) const {
			char* s = (char*)calloc(length()+right_value.length()+1, 1);

			for(int i=0,len=length(); i<len; i++) {
				s[i] = str[i];
			}

			for(int i=0,head=length(),len=right_value.length(); i<len; i++) {
				s[head+i] = right_value[i];
			}

			String rst(s);

			free(s);

			return rst;
		}

		String operator+=(const String& right_value) {
			return *(this) = *(this) + right_value;
		}

		bool operator==(const String& right_value) const {
			return equals(right_value);
		}

		bool operator!=(const String& right_value) const {
			return !equals(right_value);
		}

		char& operator[](int index) {
			return str[index];
		}

		char operator[](int index) const {
			return str[index];
		}

		~String() {
			StrFree();
		}
};